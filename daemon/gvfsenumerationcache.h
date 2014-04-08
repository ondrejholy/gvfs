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

#ifndef __G_VFS_ENUMERATION_CACHE_H__
#define __G_VFS_ENUMERATION_CACHE_H__

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _GVfsEnumerationCache GVfsEnumerationCache;

/*
 * N.B.: Max time should be set if virtual filesystem isn't readonly or
 * mutual excluded.
 */

GVfsEnumerationCache * g_vfs_enumeration_cache_new         (guint max_count,
                                                            guint max_time);
void                   g_vfs_enumeration_cache_free        (GVfsEnumerationCache *cache);
guint                  g_vfs_enumeration_cache_get_count   (GVfsEnumerationCache *cache);

gint64                 g_vfs_enumeration_cache_insert      (GVfsEnumerationCache *cache,
                                                            gchar *path);
void                   g_vfs_enumeration_cache_set         (GVfsEnumerationCache *cache,
                                                            const gchar *path,
                                                            GList *infos,
                                                            GFileAttributeMatcher *matcher,
                                                            GFileQueryInfoFlags flags,
                                                            gint64 stamp);
GList *                g_vfs_enumeration_cache_find        (GVfsEnumerationCache *cache,
                                                            const gchar *path,
                                                            GFileAttributeMatcher *matcher,
                                                            GFileQueryInfoFlags flags,
                                                            guint *count);

void                   g_vfs_enumeration_cache_invalidate  (GVfsEnumerationCache *cache,
                                                            const gchar *path,
                                                            gboolean maybe_dir);
void                   g_vfs_enumeration_cache_remove      (GVfsEnumerationCache *cache,
                                                            const gchar *path);
void                   g_vfs_enumeration_cache_remove_all  (GVfsEnumerationCache *cache);

void                   g_vfs_enumeration_cache_disable     (GVfsEnumerationCache *cache);
void                   g_vfs_enumeration_cache_enable      (GVfsEnumerationCache *cache);
gboolean               g_vfs_enumeration_cache_is_disabled (GVfsEnumerationCache *cache);

G_END_DECLS

#endif /* __G_VFS_ENUMERATION_CACHE_H__ */
