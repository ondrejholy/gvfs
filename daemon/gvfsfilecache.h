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

#ifndef __G_VFS_FILE_CACHE_H__
#define __G_VFS_FILE_CACHE_H__

#include <glib.h>
#include <gio/gio.h>
#include "gvfsjobopenforread.h"
#include "gvfsjobread.h"
#include "gvfsjobseekread.h"
#include "gvfsjobcloseread.h"

G_BEGIN_DECLS

/*
 * N.B.: Pull have to be implemented to use this cache.
 */

GVfsFileCache *       g_vfs_file_cache_new           (void);
void                  g_vfs_file_cache_free          (GVfsFileCache *cache);

void                  g_vfs_file_cache_open_for_read (GVfsFileCache *cache,
                                                      GVfsJobOpenForRead *job);
void                  g_vfs_file_cache_read          (GVfsFileCache *cache,
                                                      GVfsJobRead *job);
void                  g_vfs_file_cache_seek_read     (GVfsFileCache *cache,
                                                      GVfsJobSeekRead *job);
void                  g_vfs_file_cache_close_read    (GVfsFileCache *cache,
                                                      GVfsJobCloseRead *job);

G_END_DECLS

#endif /* __G_VFS_FILE_CACHE_H__ */
