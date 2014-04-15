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

#include "gvfsjob.h"
#include "gvfsjobopenforread.h"
#include "gvfsjobread.h"
#include "gvfsjobseekread.h"
#include "gvfsjobcloseread.h"
#include "gvfsjobpull.h"
#include "gvfsfilecache.h"

typedef struct _GVfsFileCacheHandle GVfsFileCacheHandle;

/**
 * GVfsFileCache:
 *
 * The #GVfsFileCache is an data structure to represent a cache for file data
 * to emulate standard stream operations using pull method.
 */
struct _GVfsFileCache
{
  void *reserved;
};

struct _GVfsFileCacheHandle
{
  gchar *file;
  GInputStream *stream;
};

/**
 * g_vfs_file_cache_new:
 *
 * Return value: a new #GVfsFileCache
 */
GVfsFileCache *
g_vfs_file_cache_new (void)
{
  GVfsFileCache *cache;

  cache = g_slice_new0 (GVfsFileCache);

  return cache;
}

/**
 * g_vfs_file_cache_free:
 * @cache: a #GVfsFileCache
 */
void
g_vfs_file_cache_free (GVfsFileCache *cache)
{
  g_assert (cache != NULL);

  g_slice_free (GVfsFileCache, cache);
}

static void
open_for_read_cb (GVfsJob *job_pull,
                  GVfsJob *job)
{
  GVfsJobPull *op_job_pull = G_VFS_JOB_PULL (job_pull);
  GVfsJobOpenForRead *op_job = G_VFS_JOB_OPEN_FOR_READ (job);
  GVfsFileCacheHandle *handle;
  GFileInputStream *stream;
  GFile *file;

  if (job_pull->failed)
    {
      g_vfs_job_failed_from_error (job, job_pull->error);
      g_object_unref (job_pull);
      return;
    }

  /* Open stream on temp file */
  file = g_file_new_for_path (op_job_pull->local_path);
  stream = g_file_read (file, NULL, NULL);
  if (stream)
  {
    handle = g_slice_new0 (GVfsFileCacheHandle);
    handle->stream = G_INPUT_STREAM (stream);
    handle->file = g_strdup (op_job->filename);

    op_job->backend_handle = handle;
    op_job->can_seek = g_seekable_can_seek (G_SEEKABLE (handle->stream));

    g_vfs_job_succeeded (job);
  }
  else
  {
    g_vfs_job_failed (job, G_IO_ERROR, G_IO_ERROR_FAILED, "Cache Error");
  }

  g_unlink (op_job_pull->local_path);
  g_object_unref (job_pull);
}

/**
 * g_vfs_file_cache_open_for_read:
 * @cache: a #GVfsFileCache
 * @op_job: a #GVfsJobOpenForRead
 *
 * Cache file using pull and open stream.
 */
void
g_vfs_file_cache_open_for_read (GVfsFileCache *cache,
                                GVfsJobOpenForRead *op_job)
{
  GVfsJob *job = G_VFS_JOB (op_job);
  GVfsFileCacheHandle *handle;
  GVfsJob *job_pull;
  gchar *temp;
  gint fd;

  g_assert (cache != NULL);
  g_assert (job != NULL);

  g_debug ("g_vfs_file_cache_read: %s\n", op_job->filename);

  /* Create temp file first */
  fd = g_file_open_tmp (NULL, &temp, NULL);
  if (fd < 0)
  {
    g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR, G_IO_ERROR_FAILED,
                      "Cache Error");
    return;
  }

  /* Execute pull job */
  job_pull = g_vfs_job_pull_new (op_job->filename, temp,
                                 G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_OVERWRITE,
                                 FALSE, op_job->backend);
  g_signal_connect (G_OBJECT (job_pull), "finished",
                    G_CALLBACK (open_for_read_cb), op_job);
  g_vfs_job_source_new_job (G_VFS_JOB_SOURCE (op_job->backend), job_pull);
  g_free (temp);
  g_close (fd);

  return;
}

static void
read_cb (GObject *object,
         GAsyncResult *result,
         gpointer user_data)
{
  GInputStream *stream = G_INPUT_STREAM (object);
  GVfsJob *job = G_VFS_JOB (user_data);
  GVfsJobRead *op_job = G_VFS_JOB_READ (job);
  GError *error = NULL;

  op_job->data_count = g_input_stream_read_finish (stream, result, &error);
  if (error)
  {
    g_vfs_job_failed_from_error (job, error);
    g_clear_error (&error);
    return;
  }

  g_vfs_job_succeeded (job);
}

/**
 * g_vfs_file_cache_read:
 * @cache: a #GVfsFileCache
 * @op_job: a #GVfsJobRead
 *
 * Read data from the cached file.
 */
void
g_vfs_file_cache_read (GVfsFileCache *cache,
                       GVfsJobRead *op_job)
{
  GVfsJob *job = G_VFS_JOB (op_job);
  GVfsFileCacheHandle *handle = op_job->handle;

  g_assert (cache != NULL);
  g_assert (job != NULL);

  g_debug ("g_vfs_file_cache_read: %s\n", handle->file);

  g_input_stream_read_async (handle->stream,
                             op_job->buffer,
                             op_job->bytes_requested,
                             G_PRIORITY_DEFAULT,
                             job->cancellable,
                             read_cb, job);
}

/**
 * g_vfs_file_cache_seek_read:
 * @cache: a #GVfsFileCache
 * @op_job: a #GVfsJobSeek
 *
 * Seek at the cached file stream.
 */
void
g_vfs_file_cache_seek_read (GVfsFileCache *cache,
                            GVfsJobSeekRead *op_job)
{
  GVfsJob *job = G_VFS_JOB (op_job);
  GVfsFileCacheHandle *handle = op_job->handle;
  GError *error = NULL;

  g_assert (cache != NULL);
  g_assert (job != NULL);

  g_debug ("g_vfs_file_cache_seek_read: %s\n", handle->file);

  g_seekable_seek (G_SEEKABLE (handle->stream),
                   op_job->requested_offset,
                   op_job->seek_type,
                   job->cancellable,
                   &error);
  if (error)
  {
    g_vfs_job_failed_from_error (job, error);
    g_clear_error (&error);
    return;
  }

  op_job->final_offset = g_seekable_tell (G_SEEKABLE (handle->stream));
  g_vfs_job_succeeded (job);
}

static void
close_read_cb (GObject *object,
               GAsyncResult *result,
               gpointer user_data)
{
  GInputStream *stream = G_INPUT_STREAM (object);
  GVfsJob *job = G_VFS_JOB (user_data);
  GVfsJobCloseRead *op_job = G_VFS_JOB_CLOSE_READ (job);
  GVfsFileCacheHandle *handle = op_job->handle;
  GError *error = NULL;

  /* Release handle */
  g_free (handle->file);
  g_slice_free (GVfsFileCacheHandle, handle);

  g_input_stream_close_finish (stream, result, &error);
  if (error)
  {
    g_vfs_job_failed_from_error (job, error);
    g_clear_error (&error);
    return;
  }

  g_vfs_job_succeeded (job);
}

/**
 * g_vfs_file_cache_close_read:
 * @cache: a #GVfsFileCache
 * @op_job: a #GVfsJobCloseRead
 *
 * Close the cached file stream.
 */
void
g_vfs_file_cache_close_read (GVfsFileCache *cache,
                             GVfsJobCloseRead *op_job)
{
  GVfsJob *job = G_VFS_JOB (op_job);
  GVfsFileCacheHandle *handle = op_job->handle;

  g_assert (cache != NULL);
  g_assert (op_job != NULL);

  g_debug ("file_cache_close_read: %s\n", handle->file);

  g_input_stream_close_async (handle->stream,
                              G_PRIORITY_DEFAULT,
                              job->cancellable,
                              close_read_cb, job);
}
