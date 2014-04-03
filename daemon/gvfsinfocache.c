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

#include "gvfsinfocache.h"

typedef struct _GVfsInfoCacheEntry GVfsInfoCacheEntry;

/**
 * GVfsInfoCache:
 *
 * The #GVfsInfoCache is an data structure to represent a cache for
 * #GFileInfo objects identificated by absolute file path.
 *
 * It is thread safe.
 */
struct _GVfsInfoCache
{
  guint max_count;
  guint max_time; /* usec */

  GHashTable *hash;
  GQueue *lru;

  GQueue *gc;
  gint64 gc_stamp;
  guint gc_interval;

  GMutex lock;
  gint disable_count;
};

/* Cache entry */
struct _GVfsInfoCacheEntry
{
  GFileInfo *info;
  GFileAttributeMatcher *matcher;
  GFileQueryInfoFlags flags;
  gint64 stamp;

  GList *lru_link;
  GList *gc_link;
};

/* Release memory of the cache entry */
static void
g_vfs_info_cache_entry_free (GVfsInfoCacheEntry *entry)
{
  g_assert (entry != NULL);

  g_object_unref (entry->info);
  g_file_attribute_matcher_unref (entry->matcher);

  g_slice_free (GVfsInfoCacheEntry, entry);
}

/* Check if entry may be used */
static gboolean
g_vfs_info_cache_is_entry_valid (GVfsInfoCache *cache,
                                 GVfsInfoCacheEntry *entry,
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
    if (entry->flags == G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS)
    {
      has_attribute = g_file_attribute_matcher_matches (entry->matcher,
                                                        G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK);
      if (!has_attribute || g_file_info_get_is_symlink (entry->info))
      {
        return FALSE;
      }
    }
    else
    {
      return FALSE;
    }
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
g_vfs_info_cache_remove_internal (GVfsInfoCache *cache,
                                  const gchar *path,
                                  GVfsInfoCacheEntry *entry)
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
      g_queue_delete_link (cache->lru, entry->lru_link);
    }

    if (entry->gc_link)
    {
      g_queue_delete_link (cache->gc, entry->gc_link);
    }

    g_hash_table_remove (cache->hash, path);
  }
}

/* Remove all entries from hash table and lru */
static void
g_vfs_info_cache_remove_all_internal (GVfsInfoCache *cache)
{
  g_assert (cache != NULL);

  g_hash_table_remove_all (cache->hash);
  g_queue_clear (cache->lru);
  g_queue_clear (cache->gc);

  cache->gc_stamp = g_get_real_time ();
}

/* Remove lru entry from cache if needed */
static void
g_vfs_info_cache_remove_lru (GVfsInfoCache *cache)
{
  const gchar *path;

  g_assert (cache != NULL);

  if (cache->max_count && g_queue_get_length (cache->lru) > cache->max_count)
  {
    path = g_queue_peek_head (cache->lru);
    g_vfs_info_cache_remove_internal (cache, path, NULL);
  }
}

/* Remove invalid entries from the cache */
static void
g_vfs_info_cache_garbage_collector (GVfsInfoCache *cache)
{
  GVfsInfoCacheEntry *entry;
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
        g_vfs_info_cache_remove_internal (cache, path, entry);
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
 * g_vfs_info_cache_new:
 * @max_count: maximal count of items in the cache, or 0 if you want
 *             unlimited
 * @max_time: maximal time in seconds for invalidation, or 0 if you
 *            don't want it
 *
 * Least Recently Used algorithm is used if the maximal count is set.
 *
 * Return value: a new #GVfsInfoCache
 */
GVfsInfoCache *
g_vfs_info_cache_new (guint max_count,
                      guint max_time)
{
  GVfsInfoCache *cache;

  cache = g_slice_new0 (GVfsInfoCache);

  cache->max_count = max_count;
  cache->max_time = max_time * G_USEC_PER_SEC;

  cache->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                       (GDestroyNotify)g_vfs_info_cache_entry_free);
  cache->lru = g_queue_new ();

  cache->gc = g_queue_new ();
  cache->gc_stamp = g_get_real_time ();
  cache->gc_interval = cache->max_time / 2;

  g_mutex_init (&cache->lock);

  return cache;
}

/**
 * g_vfs_info_cache_free:
 * @cache: a #GVfsInfoCache
 *
 * Return value: a new #GVfsInfoCache
 */
void
g_vfs_info_cache_free (GVfsInfoCache *cache)
{
  g_assert (cache != NULL);

  g_hash_table_destroy (cache->hash);
  g_queue_free (cache->lru);
  g_queue_free (cache->gc);
  g_mutex_clear (&cache->lock);

  g_slice_free (GVfsInfoCache, cache);
}

/**
 * g_vfs_info_cache_get_count:
 * @cache: a #GVfsInfoCache
 *
 * Return value: the number of cached items in the #GVfsInfoCache
 */
guint
g_vfs_info_cache_get_count (GVfsInfoCache *cache)
{
  g_assert (cache != NULL);

  return g_hash_table_size (cache->hash);
}

/**
 * g_vfs_info_cache_insert:
 * @cache: a #GVfsInfoCache
 * @path: a file path
 * @info: the #GFileInfo to cache for the file path
 * @matcher: a #GFileAttributeMatcher
 * @flags: a #GFileQueryInfoFlags
 *
 * Current #GFileInfo is replaced with the new #GFileInfo if the key exists in
 * the #GVfsInfoCache. 
 * 
 * The #GFileInfo is not inserted if the cache is disabled.
 */
void
g_vfs_info_cache_insert (GVfsInfoCache *cache,
                         gchar *path,
                         GFileInfo *info,
                         GFileAttributeMatcher *matcher,
                         GFileQueryInfoFlags flags)
{
  GVfsInfoCacheEntry *entry;

  g_assert (cache != NULL);
  g_assert (path != NULL);
  g_assert (info != NULL);
  g_assert (matcher != NULL);

  g_mutex_lock (&cache->lock);
  if (cache->disable_count)
  {
    g_free (path);
    g_object_unref (info);
    g_file_attribute_matcher_unref (matcher);
    g_mutex_unlock (&cache->lock);
    return;
  }

  g_debug ("info_cache_insert: %s\n", path);

  /* Remove invalid entries */
  g_vfs_info_cache_garbage_collector (cache);

  /* Remove current entry if exists */
  g_vfs_info_cache_remove_internal (cache, path, NULL);

  /* Create new entry */
  entry = g_slice_new0 (GVfsInfoCacheEntry);
  entry->info = info;
  entry->matcher = matcher;
  entry->flags = flags;
  entry->stamp = g_get_real_time ();

  g_queue_push_tail (cache->lru, path);
  entry->lru_link = g_queue_peek_tail_link (cache->lru);

  g_queue_push_tail (cache->gc, path);
  entry->gc_link = g_queue_peek_tail_link (cache->gc);

  g_hash_table_insert (cache->hash, path, entry);

  /* Remove LRU entry if needed */
  g_vfs_info_cache_remove_lru (cache);

  g_mutex_unlock (&cache->lock);
}

/**
 * g_vfs_info_cache_find:
 * @cache: a #GVfsInfoCache
 * @path: a file path
 * @matcher: a #GFileAttributeMatcher
 * @flags: a #GFileQueryInfoFlags
 *
 * The #GFileInfo is not also returned if it is invalid.
 *
 * Return value: the #GFileInfo of the file path is returned, or %NULL
 *               if it is not found
 */
GFileInfo *
g_vfs_info_cache_find (GVfsInfoCache *cache,
                       const gchar *path,
                       GFileAttributeMatcher *matcher,
                       GFileQueryInfoFlags flags)
{
  GVfsInfoCacheEntry *entry;
  GFileInfo *info = NULL;

  g_assert (cache != NULL);
  g_assert (path != NULL);
  g_assert (matcher != NULL);

  g_mutex_lock (&cache->lock);

  /* Remove invalid entries */
  g_vfs_info_cache_garbage_collector (cache);

  entry = g_hash_table_lookup (cache->hash, path);
  if (entry && g_vfs_info_cache_is_entry_valid (cache, entry, matcher, flags))
  {
    g_debug ("info_cache_find: %s\n", path);

    /* Update LRU */
    g_queue_unlink (cache->lru, entry->lru_link);
    g_queue_push_tail_link (cache->lru, entry->lru_link);

    info = g_object_ref (entry->info);
  }

  g_mutex_unlock (&cache->lock);

  return info;
}

/**
 * g_vfs_info_cache_invalidate:
 * @cache: a #GVfsInfoCache
 * @path: a file path
 * @maybe_dir: %FALSE if it is regular file, otherwise %TRUE
 *
 * If the file path already exists in the #GVfsInfoCache, it is removed
 * with its parent. All cached items are removed if @may_be dir is %TRUE
 * and file type isn't known.
 *
 * Return value: %FALSE if it isn't dir, otherwise %TRUE
 */
gboolean
g_vfs_info_cache_invalidate (GVfsInfoCache *cache,
                             const gchar *path,
                             gboolean maybe_dir)
{
  GVfsInfoCacheEntry *entry;
  GFileInfo *info;
  GFileQueryInfoFlags flags = G_FILE_QUERY_INFO_NONE;
  GFileType type;
  gboolean is_file = !maybe_dir;
  gchar *parent;

  g_assert (cache != NULL);
  g_assert (path != NULL);

  g_mutex_lock (&cache->lock);

  g_debug ("info_cache_invalidate\n");

  /* Remove invalid entries */
  g_vfs_info_cache_garbage_collector (cache);

  /* Try to determine file type */
  if (maybe_dir)
  {
    entry = g_hash_table_lookup (cache->hash, path);
    if (entry && g_vfs_info_cache_is_entry_valid (cache, entry, NULL, flags))
    {
      type = g_file_info_get_file_type (entry->info);
      is_file = (type == G_FILE_TYPE_REGULAR);
    }
  }

  /* Invalidate cache by file type */
  if (is_file)
  {
    parent = g_path_get_dirname (path);
    g_vfs_info_cache_remove_internal (cache, parent, NULL);
    g_vfs_info_cache_remove_internal (cache, path, NULL);
    g_free (parent);
  }
  else
  {
    g_vfs_info_cache_remove_all_internal (cache);
  }

  g_mutex_unlock (&cache->lock);

  return !is_file;
}

/**
 * g_vfs_info_cache_remove:
 * @cache: a #GVfsInfoCache
 * @path: a file path
 *
 * If the file path already exists in the #GVfsInfoCache, it is removed.
 */
void
g_vfs_info_cache_remove (GVfsInfoCache *cache, const gchar *path)
{
  GVfsInfoCacheEntry *entry;

  g_assert (cache != NULL);
  g_assert (path != NULL);

  g_debug ("info_cache_remove: %s\n", path);

  g_mutex_lock (&cache->lock);
  g_vfs_info_cache_remove_internal (cache, path, NULL);
  g_mutex_unlock (&cache->lock);
}

/**
 * g_vfs_info_cache_remove_all:
 * @cache: a #GVfsInfoCache
 *
 * All cached items are removed.
 */
void
g_vfs_info_cache_remove_all (GVfsInfoCache *cache)
{
  g_assert (cache != NULL);

  g_debug ("info_cache_remove_all\n");

  g_mutex_lock (&cache->lock);
  g_vfs_info_cache_remove_all_internal (cache);
  g_mutex_unlock (&cache->lock);
}

/**
 * g_vfs_info_cache_disable:
 * @cache: a #GVfsInfoCache
 *
 * Disable cache for #g_vfs_info_cache_insert. The cache should be
 * disabled during write operations.
 */
void
g_vfs_info_cache_disable (GVfsInfoCache *cache)
{
  g_assert (cache != NULL);

  g_mutex_lock (&cache->lock);

  g_debug ("info_cache_disable: %d\n", cache->disable_count);

  cache->disable_count++;
  g_mutex_unlock (&cache->lock);
}

/**
 * g_vfs_info_cache_enable:
 * @cache: a #GVfsInfoCache
 *
 * Enable disabled cache.
 */
void
g_vfs_info_cache_enable (GVfsInfoCache *cache)
{
  g_assert (cache != NULL);

  g_mutex_lock (&cache->lock);

  g_debug ("info_cache_enable: %d\n", cache->disable_count);

  g_assert (cache->disable_count > 0);

  cache->disable_count--;
  g_mutex_unlock (&cache->lock);
}

/**
 * g_vfs_info_cache_is_disabled:
 * @cache: a #GVfsInfoCache
 *
 * Return value: TRUE if disabled, FALSE if enabled
 */
gboolean
g_vfs_info_cache_is_disabled (GVfsInfoCache *cache)
{
  gboolean is_disabled;

  g_assert (cache != NULL);

  g_mutex_lock (&cache->lock);
  is_disabled = cache->disable_count ? 1 : 0;
  g_mutex_unlock (&cache->lock);

  return is_disabled;
}
