/* GIO - GLib Input, Output and Streaming Library
 *
 * Copyright (C) 2014 Ondrej Holy <oholy@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <gio/gio.h>

#include "gvfsenumerationcache.h"

typedef struct _GVfsEnumerationCacheEntry GVfsEnumerationCacheEntry;

#define LRU_COUNT 5 /* Number of LRU Size Adjusted lists */

/**
 * GVfsEnumerationCache:
 *
 * The #GVfsEnumerationCache is an data structure to represent a cache for
 * enumeration of #GFileInfo objects identificated by absolute dir path.
 *
 * It is thread safe.
 */
struct _GVfsEnumerationCache
{
  guint max_count;
  guint max_time; /* usec */

  GHashTable *hash;
  GQueue *lru[LRU_COUNT];
  guint count;

  GQueue *gc;
  gint64 gc_stamp;
  guint gc_interval;

  GMutex lock;
  gint disable_count;
};

/* Cache entry */
struct _GVfsEnumerationCacheEntry
{
  GList *infos; /* of #GFileInfo */
  GFileAttributeMatcher *matcher;
  GFileQueryInfoFlags flags;
  gint64 stamp;
  guint count;

  GQueue *lru;
  GList *lru_link;
  GList *gc_link;
};

/* Release memory of the cache entry */
static void
g_vfs_enumeration_cache_entry_free (GVfsEnumerationCacheEntry *entry)
{
  g_assert (entry != NULL);

  g_list_free_full (entry->infos, g_object_unref);
  g_file_attribute_matcher_unref (entry->matcher);

  g_slice_free (GVfsEnumerationCacheEntry, entry);
}

/* Check if entry may be used */
static gboolean
g_vfs_enumeration_cache_is_entry_valid (GVfsEnumerationCache *cache,
                                        GVfsEnumerationCacheEntry *entry,
                                        GFileAttributeMatcher *matcher,
                                        GFileQueryInfoFlags flags)
{
  GFileAttributeMatcher *attribute_matcher;
  gboolean has_attribute;
  gint64 time;

  g_assert (cache != NULL);
  g_assert (entry != NULL);

  /* Check if time stamp is valid */
  time = g_get_real_time ();
  if (cache->max_time && time - entry->stamp > cache->max_time)
  {
    return FALSE;
  }

  /* Check if flags are substituable */
  if (entry->flags != flags)
  {
    return FALSE;
  }

  /* Check if attribute matcher is wider */
  attribute_matcher = g_file_attribute_matcher_subtract (matcher,
                                                         entry->matcher);
  if (attribute_matcher)
  {
    g_file_attribute_matcher_unref (attribute_matcher);
    return FALSE;
  }

  return TRUE;
}

/* Remove entry from hash table and lru */
static void
g_vfs_enumeration_cache_remove_internal (GVfsEnumerationCache *cache,
                                         const gchar *path,
                                         GVfsEnumerationCacheEntry *entry)
{
  g_assert (cache != NULL);
  g_assert (path != NULL);

  if (!entry)
  {
    entry = g_hash_table_lookup (cache->hash, path);
  }

  if (entry)
  {
    if (entry->lru_link)
    {
      g_queue_delete_link (entry->lru, entry->lru_link);
    }

    if (entry->gc_link)
    {
      g_queue_delete_link (cache->gc, entry->gc_link);
    }

    cache->count -= entry->count;
    g_hash_table_remove (cache->hash, path);
  }
}

/* Remove all entries from hash table and lru */
static void
g_vfs_enumeration_cache_remove_all_internal (GVfsEnumerationCache *cache)
{
  gint i;

  g_assert (cache != NULL);

  g_hash_table_remove_all (cache->hash);
  g_queue_clear (cache->gc);

  for (i = 0; i < LRU_COUNT; i++)
  {
    g_queue_clear (cache->lru[i]);
  }

  cache->gc_stamp = g_get_real_time ();
  cache->count = 0;
}

/* Remove lru entries from cache if needed */
static void
g_vfs_enumeration_cache_remove_lru (GVfsEnumerationCache *cache)
{
  GVfsEnumerationCacheEntry *entry, *max_entry;
  const gchar *path, *max_path;
  guint64 time;
  gint i, value, max = 0;

  g_assert (cache != NULL);

  time = g_get_real_time ();
  while (cache->max_count && cache->count > cache->max_count)
  {
    /* Remove entry with biggest value of (time_in_cache * count). */
    for (i = 0; i < LRU_COUNT; i++)
    {
      path = g_queue_peek_head (cache->lru[i]);
      if (!path)
      {
        continue;
      }

      entry = g_hash_table_lookup (cache->hash, path);

      g_assert (entry != NULL);

      value = entry->count * (time - entry->stamp);
      if (value >= max)
      {
        max = value;
        max_path = path;
        max_entry = entry;
      }
    }

    g_vfs_enumeration_cache_remove_internal (cache, max_path, max_entry);
  }
}

/* Remove invalid entries from the cache */
static void
g_vfs_enumeration_cache_garbage_collector (GVfsEnumerationCache *cache)
{
  GVfsEnumerationCacheEntry *entry;
  const gchar *path;
  gint64 time;

  g_assert (cache != NULL);

  time = g_get_real_time ();
  if (cache->gc_interval && time - cache->gc_stamp > cache->gc_interval)
  {
    while (TRUE)
    {
      path = g_queue_peek_head (cache->gc);
      if (!path)
      {
        break;
      }

      entry = g_hash_table_lookup (cache->hash, path);
      if (entry && time - entry->stamp > cache->max_time)
      {
        g_vfs_enumeration_cache_remove_internal (cache, path, entry);
      }
      else
      {
        break;
      }
    }

    cache->gc_stamp = time;
  }
}

/**
 * g_vfs_enumeration_cache_new:
 * @max_count: maximal count of #GFileInfo in the cache, or 0 if you want
 *             unlimited
 * @max_time: maximal time in seconds for invalidation, or 0 if you
 *            don't want it
 *
 * Least Recently Used Size Adjustes algorithm is used if the maximal count
 * is set.
 *
 * Return value: a new #GVfsEnumerationCache
 */
GVfsEnumerationCache *
g_vfs_enumeration_cache_new (guint max_count,
                             guint max_time)
{
  GVfsEnumerationCache *cache;
  int i;

  cache = g_slice_new0 (GVfsEnumerationCache);

  cache->max_count = max_count;
  cache->max_time = max_time * G_USEC_PER_SEC;

  cache->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                       (GDestroyNotify)g_vfs_enumeration_cache_entry_free);

  for (i = 0; i < LRU_COUNT; i++)
  {
    cache->lru[i] = g_queue_new ();
  }

  cache->gc = g_queue_new ();
  cache->gc_stamp = g_get_real_time ();
  cache->gc_interval = cache->max_time / 2;

  g_mutex_init (&cache->lock);

  return cache;
}

/**
 * g_vfs_enumeration_cache_free:
 * @cache: a #GVfsEnumerationCache
 *
 * Return value: a new #GVfsEnumerationCache
 */
void
g_vfs_enumeration_cache_free (GVfsEnumerationCache *cache)
{
  gint i;

  g_assert (cache != NULL);

  g_hash_table_destroy (cache->hash);
  g_queue_free (cache->gc);
  g_mutex_clear (&cache->lock);

  for (i = 0; i < LRU_COUNT; i++)
  {
    g_queue_free (cache->lru[i]);
  }

  g_slice_free (GVfsEnumerationCache, cache);
}

/**
 * g_vfs_enumeration_cache_get_count:
 * @cache: a #GVfsEnumerationCache
 *
 * Return value: the number of cached items in the #GVfsEnumerationCache
 */
guint
g_vfs_enumeration_cache_get_count (GVfsEnumerationCache *cache)
{
  g_assert (cache != NULL);

  return cache->count;
}

/**
 * g_vfs_enumeration_cache_insert:
 * @cache: a #GVfsEnumerationCache
 * @path: a dir path
 *
 * Current enumeration is replaced by the new one if the key exists in
 * the #GVfsEnumerationCache. 
 * 
 * The enumeration is not inserted if the cache is disabled.
 * 
 * Return value: time stamp for #g_vfs_enumeration_cache_set 
 */
gint64
g_vfs_enumeration_cache_insert (GVfsEnumerationCache *cache,
                                gchar *path)
{
  GVfsEnumerationCacheEntry *entry;
  gint64 stamp = g_get_real_time ();

  g_assert (cache != NULL);
  g_assert (path != NULL);

  g_mutex_lock (&cache->lock);
  if (cache->disable_count)
  {
    g_free (path);
    g_mutex_unlock (&cache->lock);
    return stamp;
  }

  g_debug ("enumeration_cache_insert: %s\n", path);

  /* Remove invalid entries */
  g_vfs_enumeration_cache_garbage_collector (cache);

  /* Remove current entry if exists */
  g_vfs_enumeration_cache_remove_internal (cache, path, NULL);

  /* Create new entry */
  entry = g_slice_new0 (GVfsEnumerationCacheEntry);
  entry->stamp = stamp;

  g_queue_push_tail (cache->gc, path);
  entry->gc_link = g_queue_peek_tail_link (cache->gc);

  g_hash_table_insert (cache->hash, path, entry);

  g_mutex_unlock (&cache->lock);

  return stamp;
}

/* Count to lru id mapping */
static guint
count_to_lru (guint count)
{
  guint lru = 0;

  while ((count >>= 2) && lru < LRU_COUNT - 1)
  {
    lru++;
  }

  return lru;
}

/**
 * g_vfs_enumeration_cache_insert:
 * @cache: a #GVfsEnumerationCache
 * @path: a dir path
 * @infos: the #GList of #GFileInfo to cache for the file path
 * @matcher: a #GFileAttributeMatcher
 * @flags: a #GFileQueryInfoFlags
 * @stamp: the stamp returned from #g_vfs_enumeration_cache_insert
 *
 * The #g_vfs_enumeration_cache_insert has to be called first.
 */
void
g_vfs_enumeration_cache_set (GVfsEnumerationCache *cache,
                             const gchar *path,
                             GList *infos,
                             GFileAttributeMatcher *matcher,
                             GFileQueryInfoFlags flags,
                             gint64 stamp,
                             guint count)
{
  GVfsEnumerationCacheEntry *entry = NULL;
  gchar *orig_path;

  g_assert (cache != NULL);
  g_assert (path != NULL);
  g_assert (matcher != NULL);

  g_mutex_lock (&cache->lock);

  /* Update entry if it has same stamp and cache is big enough */
  g_hash_table_lookup_extended (cache->hash, path,
                                (gpointer *)&orig_path,
                                (gpointer *)&entry);
  if (entry && entry->stamp == stamp &&
      (!cache->max_count || count <= cache->max_count))
  {
    g_debug ("enumeration_cache_set: %s\n", path);

    entry->infos = infos;
    entry->matcher = matcher;
    entry->flags = flags;
    entry->count = count;

    entry->lru = cache->lru[count_to_lru (count)];
    g_queue_push_tail (entry->lru, orig_path);
    entry->lru_link = g_queue_peek_tail_link (entry->lru);

    /* Remove LRU entry if needed */
    cache->count += count;
    g_vfs_enumeration_cache_remove_lru (cache);
  }
  else
  {
    if (entry)
    {
      g_vfs_enumeration_cache_remove_internal (cache, path, entry);
    }

    g_file_attribute_matcher_unref (matcher);
    g_list_free_full (infos, g_object_unref);
  }

  g_mutex_unlock (&cache->lock);
}

/**
 * g_vfs_enumeration_cache_find:
 * @cache: a #GVfsEnumerationCache
 * @path: a dir path
 * @matcher: a #GFileAttributeMatcher
 * @flags: a #GFileQueryInfoFlags
 * @count: a size of list, or %G_MAXUINT if not found
 *
 * The #GList is not also returned if it is invalid.
 *
 * Return value: the #GList of #GFileInfo for the dir path is returned,
 *               or %NULL if it is not found
 */
GList *
g_vfs_enumeration_cache_find (GVfsEnumerationCache *cache,
                              const gchar *path,
                              GFileAttributeMatcher *matcher,
                              GFileQueryInfoFlags flags,
                              guint *count)
{
  GVfsEnumerationCacheEntry *entry;
  GList *infos = NULL;

  g_assert (cache != NULL);
  g_assert (path != NULL);
  g_assert (matcher != NULL);

  g_mutex_lock (&cache->lock);

  /* Remove invalid entries */
  g_vfs_enumeration_cache_garbage_collector (cache);

  *count = G_MAXUINT;
  entry = g_hash_table_lookup (cache->hash, path);
  if (entry && g_vfs_enumeration_cache_is_entry_valid (cache, entry, matcher, flags))
  {
    g_debug ("enumeration_cache_find: %s\n", path);

    /* Update LRU */
    g_queue_unlink (entry->lru, entry->lru_link);
    g_queue_push_tail_link (entry->lru, entry->lru_link);

   infos = g_list_copy_deep (entry->infos, (GCopyFunc)g_object_ref, NULL);
   *count = entry->count;
  }

  g_mutex_unlock (&cache->lock);

  return infos;
}

/**
 * g_vfs_enumeration_cache_invalidate:
 * @cache: a #GVfsEnumerationCache
 * @path: a file path
 * @maybe_dir: %FALSE if it is regular file, otherwise %TRUE
 *
 * If the file path already exists in the #GVfsEnumerationCache, it is removed
 * with its parent. All cached items are removed if @may_be dir is %TRUE.
 */
void
g_vfs_enumeration_cache_invalidate (GVfsEnumerationCache *cache,
                                    const gchar *path,
                                    gboolean maybe_dir)
{
  GVfsEnumerationCacheEntry *entry;
  gboolean is_file = !maybe_dir;
  gchar *parent;

  g_assert (cache != NULL);
  g_assert (path != NULL);

  g_mutex_lock (&cache->lock);

  g_debug ("enumeration_cache_invalidate\n");

  /* Remove invalid entries */
  g_vfs_enumeration_cache_garbage_collector (cache);

  /* Invalidate cache by file type */
  if (is_file)
  {
    parent = g_path_get_dirname (path);
    g_vfs_enumeration_cache_remove_internal (cache, parent, NULL);
    g_vfs_enumeration_cache_remove_internal (cache, path, NULL);
    g_free (parent);
  }
  else
  {
    g_vfs_enumeration_cache_remove_all_internal (cache);
  }

  g_mutex_unlock (&cache->lock);
}

/**
 * g_vfs_enumeration_cache_remove:
 * @cache: a #GVfsEnumerationCache
 * @path: a dir path
 *
 * If the dir path already exists in the #GVfsEnumerationCache, it is removed.
 */
void
g_vfs_enumeration_cache_remove (GVfsEnumerationCache *cache, const gchar *path)
{
  GVfsEnumerationCacheEntry *entry;

  g_assert (cache != NULL);
  g_assert (path != NULL);

  g_debug ("enumeration_cache_remove: %s\n", path);

  g_mutex_lock (&cache->lock);
  g_vfs_enumeration_cache_remove_internal (cache, path, NULL);
  g_mutex_unlock (&cache->lock);
}

/**
 * g_vfs_enumeration_cache_remove_all:
 * @cache: a #GVfsEnumerationCache
 *
 * All cached items are removed.
 */
void
g_vfs_enumeration_cache_remove_all (GVfsEnumerationCache *cache)
{
  g_assert (cache != NULL);

  g_debug ("enumeration_cache_remove_all\n");

  g_mutex_lock (&cache->lock);
  g_vfs_enumeration_cache_remove_all_internal (cache);
  g_mutex_unlock (&cache->lock);
}

/**
 * g_vfs_enumeration_cache_disable:
 * @cache: a #GVfsEnumerationCache
 *
 * Disable cache for #g_vfs_enumeration_cache_insert. The cache should be
 * disabled during write operations.
 */
void
g_vfs_enumeration_cache_disable (GVfsEnumerationCache *cache)
{
  g_assert (cache != NULL);

  g_mutex_lock (&cache->lock);

  g_debug ("enumeration_cache_disable: %d\n", cache->disable_count);

  cache->disable_count++;
  g_mutex_unlock (&cache->lock);
}

/**
 * g_vfs_enumeration_cache_enable:
 * @cache: a #GVfsEnumerationCache
 *
 * Enable disabled cache.
 */
void
g_vfs_enumeration_cache_enable (GVfsEnumerationCache *cache)
{
  g_assert (cache != NULL);

  g_mutex_lock (&cache->lock);

  g_debug ("enumeration_cache_enable: %d\n", cache->disable_count);

  g_assert (cache->disable_count > 0);

  cache->disable_count--;
  g_mutex_unlock (&cache->lock);
}

/**
 * g_vfs_enumeration_cache_is_disabled:
 * @cache: a #GVfsEnumerationCache
 *
 * Return value: TRUE if disabled, FALSE if enabled
 */
gboolean
g_vfs_enumeration_cache_is_disabled (GVfsEnumerationCache *cache)
{
  gboolean is_disabled;

  g_assert (cache != NULL);

  g_mutex_lock (&cache->lock);
  is_disabled = cache->disable_count ? 1 : 0;
  g_mutex_unlock (&cache->lock);

  return is_disabled;
}
