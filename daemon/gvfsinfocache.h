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

#ifndef __G_VFS_INFO_CACHE_H__
#define __G_VFS_INFO_CACHE_H__

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _GVfsInfoCache GVfsInfoCache;

/*
 * N.B.: Max time should be set if virtual filesystem isn't readonly or
 * mutual excluded. Max count also should be set to limit memory
 * requirements, because invalid items isn't automatically removed. 
 */

GVfsInfoCache * g_vfs_info_cache_new         (guint max_count,
                                              guint max_time);
void            g_vfs_info_cache_free        (GVfsInfoCache *cache);
guint           g_vfs_info_cache_get_count   (GVfsInfoCache *cache);

void            g_vfs_info_cache_insert      (GVfsInfoCache *cache,
                                              gchar *path,
                                              GFileInfo *info,
                                              GFileAttributeMatcher *matcher,
                                              GFileQueryInfoFlags flags);
GFileInfo *     g_vfs_info_cache_find        (GVfsInfoCache *cache,
                                              const gchar *path,
                                              GFileAttributeMatcher *matcher,
                                              GFileQueryInfoFlags flags);

gboolean        g_vfs_info_cache_invalidate  (GVfsInfoCache *cache,
                                              const gchar *path,
                                              gboolean maybe_dir);
void            g_vfs_info_cache_remove      (GVfsInfoCache *cache,
                                              const gchar *path);
void            g_vfs_info_cache_remove_all  (GVfsInfoCache *cache);

void            g_vfs_info_cache_disable     (GVfsInfoCache *cache);
void            g_vfs_info_cache_enable      (GVfsInfoCache *cache);
gboolean        g_vfs_info_cache_is_disabled (GVfsInfoCache *cache);

G_END_DECLS

#endif /* __G_VFS_INFO_CACHE_H__ */
