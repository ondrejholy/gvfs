/* GIO - GLib Input, Output and Streaming Library
 * 
 * Copyright (C) 2008 Benjamin Otte <otte@gnome.org>
 *               2012 Ondrej Holy <xholyo00@stud.fit.vutbr.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Benjmain Otte <otte@gnome.org>
 *         Ondrej Holy <xholyo00@stud.fit.vutbr.com>
 */

/** @file gvfsbackendarchive.c */

#include <config.h>

#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <string.h>
#include <archive.h>
#include <archive_entry.h>

#include "gvfsbackendarchive.h"
#include "gvfsjobopenforread.h"
#include "gvfsjobread.h"
#include "gvfsjobseekread.h"
#include "gvfsjobopenforwrite.h"
#include "gvfsjobwrite.h"
#include "gvfsjobseekwrite.h"
#include "gvfsjobsetdisplayname.h"
#include "gvfsjobqueryinfo.h"
#include "gvfsjobqueryfsinfo.h"
#include "gvfsjobqueryattributes.h"
#include "gvfsjobenumerate.h"
#include "gvfsdaemonprotocol.h"
#include "gvfsdaemonutils.h"
#include "gvfskeyring.h"

/** Icon of the backend. @showinitializer */
#define MOUNT_ICON_NAME "drive-removable-media"

/* #define PRINT_DEBUG */

/** 
 * Print debug information @showinitializer 
 * @note Enable using macro ::PRINT_DEBUG. 
 */
#ifdef PRINT_DEBUG
#define DEBUG g_print
#else
#define DEBUG(...)
#endif

/*** TYPE DEFINITIONS ***/

typedef struct _ArchiveFile ArchiveFile;

/** Structure to represent file of virtual tree. */
struct _ArchiveFile {
  char *                name;           /**< The name of the file inside the archive. */
  GFileInfo *           info;           /**< The file info created from the archive entry. */
  GSList *              children;       /**< The list of the child files. */
  ArchiveFile *         parent;         /**< The parent of the archive file. */
};

/** Structure to represent class members. */
struct _GVfsBackendArchive
{
  GVfsBackend           backend;        /**< The backend class. */

  GFile *               file;           /**< The archive file. */
  ArchiveFile *         files;          /**< The tree of files. */
  
  int                   format;         /**< The format of the archive file. */
  int                   filters_count;  /**< The number of the filters. */
  int *                 filters;        /**< The filters of the archive. */
  
  gboolean              writable;       /**< Tell if LibArchive can write this format. */
  
  GMutex *              write_lock;     /**< The mutex to lock during changes. */
  GMutex *              read_lock;      /**< The mutex to lock during reading. */
};

G_DEFINE_TYPE (GVfsBackendArchive, g_vfs_backend_archive, G_VFS_TYPE_BACKEND)

static void backend_unmount (GVfsBackendArchive *ba);

/*** AN ARCHIVE WE CAN OPERATE ON ***/

/** The size of the copy buffers. @showinitializer */
#define BLOCKSIZE 10240

/** Structure to represent archive handler. */
typedef struct 
{
  struct archive *      archive;        /**< The archive handler for reading. */
  GFile *               file;           /**< The archive file. */
  GFileInputStream *    stream;         /**< The stream for reading the file. */
  
  struct archive *      temp_archive;   /**< The archive handler for writing. */
  GFile *               temp_file;      /**< The temporary archive file. */
  GFileOutputStream *   temp_stream;    /**< The stream for writing the file. */
  
  GVfsJob *             job;            /**< The job which is processed. */
  guchar                data[BLOCKSIZE];/**< The buffer for IO operations. */
  GError *              error;          /**< The error of the archive. */
} GVfsArchive;

/**
 * Tell if the archive is in error.
 * @param archive Initialized archive structure ::GVfsArchive.
 * @return TRUE if archive is in error.
 * @see gvfs_archive_set_error_from_errno
 */
#define gvfs_archive_in_error(archive) ((archive)->error != NULL)

/**
 * Open the archive file input stream.
 * @note It is a LibArchive callback.
 * @param archive Archive read handler.
 * @param data ::GVfsArchive data structure.
 * @return ARCHIVE_OK on success, else ARCHIVE_FATAL.
 * @see gvfs_archive_read_close, gvfs_archive_read, 
 *      gvfs_archive_read_skip, gvfs_archive_read_seek
 */
static int
gvfs_archive_read_open (struct archive *archive, 
                        void           *data)
{
  GVfsArchive *d = data;

  DEBUG ("OPEN (read)\n");
  g_assert (d->stream == NULL);
  
  d->stream = g_file_read (d->file,
			   d->job->cancellable,
			   &d->error);

  return d->error ? ARCHIVE_FATAL : ARCHIVE_OK;
}

/**
 * Open the archive file output stream.
 * @note It is a LibArchive callback.
 * @param archive Archive write handler.
 * @param data ::GVfsArchive data structure.
 * @return ARCHIVE_OK on success, else ARCHIVE_FATAL.
 * @see gvfs_archive_write_close, gvfs_archive_write
 */
static int
gvfs_archive_write_open (struct archive *archive, 
                         void           *data)
{
  GVfsArchive *d = data;

  DEBUG ("OPEN (write)\n");
  g_assert (d->temp_stream == NULL);
  
  d->temp_stream = g_file_replace (d->temp_file,
                                   NULL,
                                   FALSE,
                                   G_FILE_CREATE_REPLACE_DESTINATION,
                                   d->job->cancellable,
                                   &d->error);

  return d->error ? ARCHIVE_FATAL : ARCHIVE_OK;
}

/**
 * Read data from the archive file input stream.
 * @note It is a LibArchive callback.
 * @param archive Archive read handler.
 * @param data ::GVfsArchive data structure.
 * @param buffer Pointer to the available data (set by callback).
 * @return Count of the read bytes, 0 on error.
 * @see gvfs_archive_read_close, gvfs_archive_read_open, 
 *      gvfs_archive_read_skip, gvfs_archive_read_seek
 */
static ssize_t
gvfs_archive_read (struct archive *archive, 
		   void           *data,
		   const void    **buffer)
{
  GVfsArchive *d = data;
  gssize read_bytes;

  *buffer = d->data;
  read_bytes = g_input_stream_read (G_INPUT_STREAM (d->stream),
				    d->data,
				    sizeof (d->data),
				    d->job->cancellable,
				    &d->error);

  DEBUG ("READ %d\n", (int) read_bytes);
  return read_bytes;
}

/**
 * Write data to the archive file output stream.
 * @note It is a LibArchive callback.
 * @param archive Archive write handler.
 * @param data ::GVfsArchive data structure.
 * @param buffer Data to write.
 * @param length Count of bytes to write.
 * @return Count of the wrote bytes, -1 on error.
 * @see gvfs_archive_write_close, gvfs_archive_write_open
 */
static ssize_t 
gvfs_archive_write (struct archive *archive, 
                    void           *data, 
                    const void     *buffer, 
                    size_t          length)
{
  GVfsArchive *d = data;
  gssize write_bytes;
  
  if (gvfs_archive_in_error (d))
    return -1;
  
  write_bytes = g_output_stream_write (G_OUTPUT_STREAM (d->temp_stream),
                                       buffer,
                                       length,
                                       d->job->cancellable,
                                       &d->error);
  
  DEBUG ("WRITE %d (%d)\n", (int) write_bytes, length);
    
  return write_bytes;
}

/**
 * Seek in the archive file input stream.
 * @note It is a LibArchive callback. 
 *       The interface to the seek callback is the same as the standard fseek().
 * @param archive Archive read handler.
 * @param data ::GVfsArchive data structure.
 * @param request Offset for the new position .
 * @param whence  Position in the stream.
 * @return Actual position in the stream, ARCHIVE_FATAL on error.
 * @see gvfs_archive_read_close, gvfs_archive_read_open, 
 *      gvfs_archive_read_skip, gvfs_archive_read
 */
static off_t
gvfs_archive_read_seek (struct archive *archive,
                        void           *data, 
                        off_t           request, 
                        int             whence)
{
  GVfsArchive *d = data;
  GSeekType g_whence;
  gboolean result;

  switch (whence)
    {
      case SEEK_SET: 
        g_whence = G_SEEK_SET;
        break;
      case SEEK_CUR:
        g_whence = G_SEEK_CUR;
        break;
      case SEEK_END:
        g_whence = G_SEEK_END;
        break;
      default:
        DEBUG ("unknown seek type (%d)\n", whence);
        return ARCHIVE_FATAL;
    }
    
  if (g_seekable_can_seek (G_SEEKABLE (d->stream)))
    {
      result = g_seekable_seek (G_SEEKABLE (d->stream),
                                request,
                                g_whence,
                                d->job->cancellable,
                                &d->error);
      if (!result)
        {
          g_clear_error (&d->error);
          
          return ARCHIVE_FATAL;
        }
    }
  else
    return ARCHIVE_FATAL;
  
  DEBUG ("SEEK %d (%d)\n", (int) request,
      (int) g_seekable_tell (G_SEEKABLE (d->stream)));

  return g_seekable_tell (G_SEEKABLE (d->stream));
}

/**
 * Skip in the archive file input stream.
 * @note It is a LibArchive callback.
 * @param archive Archive read handler.
 * @param data ::GVfsArchive data structure.
 * @param request Count of the bytes to skip.
 * @return Count of the skipped bytes.
 * @see gvfs_archive_read_close, gvfs_archive_read_open, 
 *      gvfs_archive_read, gvfs_archive_read_seek
 */
static off_t
gvfs_archive_read_skip (struct archive *archive,
                        void           *data,
                        off_t           request)
{
  if (gvfs_archive_read_seek (archive, data, request, SEEK_CUR) < ARCHIVE_OK)
    return 0;
  
  return request;
}

/**
 * Close the archive file input stream.
 * @note It is a LibArchive callback.
 * @param archive Archive read handler.
 * @param data ::GVfsArchive data structure.
 * @return ARCHIVE_OK on success.
 * @see gvfs_archive_read, gvfs_archive_read_open, 
 *      gvfs_archive_read_skip, gvfs_archive_read_seek
 */
static int
gvfs_archive_read_close (struct archive *archive,
                         void           *data)
{
  GVfsArchive *d = data;
  
  DEBUG ("CLOSE (read)\n");
  
  g_object_unref (d->stream);
  d->stream = NULL;

  return ARCHIVE_OK;
}

/**
 * Close the archive file output stream.
 * @note It is a LibArchive callback.
 * @param archive Archive write handler.
 * @param data ::GVfsArchive data structure.
 * @return ARCHIVE_OK on success.
 * @see gvfs_archive_write_close, gvfs_archive_write_close
 */
static int
gvfs_archive_write_close (struct archive *archive,
                          void           *data)
{
  GVfsArchive *d = data;
  
  DEBUG ("CLOSE (write)\n");
  
  g_object_unref (d->temp_stream);
  d->temp_stream = NULL;
  
  return ARCHIVE_OK;
}

/**
 * Set the ::GVfsArchive error from the LibArchive error.
 * @param archive ::GVfsArchive data structure.
 * @see gvfs_archive_in_error
 */
static void
gvfs_archive_set_error_from_errno (GVfsArchive *archive)
{
  struct archive *error_archive = NULL;
  
  if (gvfs_archive_in_error (archive))
    return;
  
  /* Find an archive structure in error. */
  if (archive->archive != NULL)
    if (archive_errno (archive->archive) != ARCHIVE_OK)
      error_archive = archive->archive;
  if (archive->temp_archive != NULL && error_archive == NULL)
    if (archive_errno (archive->temp_archive) != ARCHIVE_OK)
      error_archive = archive->temp_archive;
  
  g_assert (error_archive != NULL);
  
  archive->error = g_error_new_literal (G_IO_ERROR, 
                     g_io_error_from_errno (archive_errno (error_archive)), 
                     archive_error_string (error_archive));
}

/**
 * Push the operation job to the ::GVfsArchive structure.
 * @param archive ::GVfsArchive data structure.
 * @param job Operation job.
 * @see gvfs_archive_pop_job, gvfs_archive_new
 */
static void 
gvfs_archive_push_job (GVfsArchive *archive,
                       GVfsJob *job)
{
  g_assert (job != NULL);
  DEBUG ("pushing job %s\n", G_OBJECT_TYPE_NAME (job));
  
  archive->job = job;
}

/**
 * Pop the operation job from the ::GVfsArchive structure.
 * @note Function set the result of the GVFS operation.
 * @param archive ::GVfsArchive data structure.
 * @see gvfs_archive_push_job, gvfs_archive_free
 */
static void 
gvfs_archive_pop_job (GVfsArchive *archive)
{
  g_assert (archive->job != NULL);    
  DEBUG ("popping job %s\n", G_OBJECT_TYPE_NAME (archive->job));
  
  if (gvfs_archive_in_error (archive))
    {
      g_vfs_job_failed_from_error (archive->job, archive->error);
      g_clear_error (&archive->error);
    }
  else
    g_vfs_job_succeeded (archive->job);

  archive->job = NULL;
}

/**
 * Free the ::GVfsArchive structure.
 * @note It moves the temporary archive over original if no error.
 * @param archive ::GVfsArchive data structure.
 * @param pop TRUE to pop the result of the GVFS operation.
 * @see gvfs_archive_new, gvfs_archive_pop, gvfs_archive_finish
 */
static void
gvfs_archive_free (GVfsArchive *archive, 
                   gboolean     pop)
{        
  if (archive->temp_archive != NULL)
    {
      archive_write_free (archive->temp_archive);
      
      /* Replace the archive file by the temporary file. */
      if (!gvfs_archive_in_error (archive))
        {
          g_file_move (archive->temp_file, 
                       archive->file, 
                       G_FILE_COPY_OVERWRITE | G_FILE_COPY_TARGET_DEFAULT_PERMS, 
                       archive->job->cancellable, 
                       NULL, 
                       NULL, 
                       &archive->error);
        }
      else
        g_file_delete (archive->temp_file, 
                       NULL, 
                       NULL);
    } 
  
  if (archive->archive != NULL)
    archive_read_free (archive->archive);
 
  if (pop)
    gvfs_archive_pop_job (archive);

  g_clear_error (&archive->error);
  g_slice_free (GVfsArchive, archive);
}

/**
 * Free the ::GVfsArchive structure and pop the GVFS operation job.
 * @note For more information look on ::gvfs_archive_free.
 * @param archive ::GVfsArchive data structure.
 * @see gvfs_archive_new, gvfs_archive_pop, gvfs_archive_free
 */
#define gvfs_archive_finish(archive) gvfs_archive_free ((archive), TRUE);

#if ARCHIVE_VERSION_NUMBER < 3000200
/**
 * Set the filter based on the code.
 * @note It is new LibArchive function (only for backward compatibility).
 * @param a LibArchive archive handler.
 * @param code Filter code to set.
 * @return ARCHIVE_OK on success, ARCHIVE_FATAL on error.
 */
static int
archive_write_add_filter(struct archive *a, int code)
{
  int i;
  
  /* A table that maps filter codes to functions. */
  struct { int code; int (*setter)(struct archive *); } codes[] =
  {
    { ARCHIVE_FILTER_NONE,     archive_write_add_filter_none },
    { ARCHIVE_FILTER_GZIP,     archive_write_add_filter_gzip },
    { ARCHIVE_FILTER_BZIP2,    archive_write_add_filter_bzip2 },
    { ARCHIVE_FILTER_COMPRESS, archive_write_add_filter_compress },
    { ARCHIVE_FILTER_LZMA,     archive_write_add_filter_lzma },
    { ARCHIVE_FILTER_XZ,       archive_write_add_filter_xz },
    { ARCHIVE_FILTER_LZIP,     archive_write_add_filter_lzip },
    { -1,                      NULL }
  };
  
  for (i = 0; codes[i].code != -1; i++) 
    if (code == codes[i].code)
      return ((codes[i].setter)(a));
  
  archive_set_error(a, EINVAL, "No such filter");
  
  return (ARCHIVE_FATAL);
}
#endif

/**
 * Create and initialize the new readonly ::GVfsArchive structure.
 * @note For more information look on ::gvfs_archive_new.
 * @param backend ::GVfsBackendArchive data structure.
 * @param job GVFS operation job.
 * @return Initialized ::GVfsArchive data structure, NULL on error
 * @see gvfs_archive_new, gvfs_archive_write_new, 
 *      gvfs_archive_readwrite_new, gvfs_archive_push_job
 */
#define gvfs_archive_read_new(backend, job) \
  gvfs_archive_new ((backend), (job), TRUE, FALSE)

/**
 * Create and initialize the new writeonly ::GVfsArchive structure.
 * @note For more information look on ::gvfs_archive_new.
 * @param backend ::GVfsBackendArchive data structure.
 * @param job GVFS operation job.
 * @return Initialized ::GVfsArchive data structure, NULL on error
 * @see gvfs_archive_read_new, gvfs_archive_new, 
 *      gvfs_archive_readwrite_new, gvfs_archive_push_job
 */
#define gvfs_archive_write_new(backend, job) \
  gvfs_archive_new ((backend), (job), FALSE, TRUE)

/**
 * Create and initialize the new readwrite ::GVfsArchive structure.
 * @note For more information look on ::gvfs_archive_new.
 * @param backend ::GVfsBackendArchive data structure.
 * @param job GVFS operation job.
 * @return Initialized ::GVfsArchive data structure, NULL on error
 * @see gvfs_archive_read_new, gvfs_archive_write_new, 
 *      gvfs_archive_new, gvfs_archive_push_job
 */
#define gvfs_archive_readwrite_new(backend, job) \
  gvfs_archive_new ((backend), (job), TRUE, TRUE)

/**
 * Create and initialize the new ::GVfsArchive structure.
 * @note It pushes the GVFS operation job. 
 *       It must be closed by ::gvfs_archive_free.
 * @param ba ::GVfsBackendArchive data structure.
 * @param job GVFS operation job.
 * @param readable Tell if the archive should be readable.
 * @param writeable Tell if the archive should be writable.
 * @return Initialized ::GVfsArchive data structure, NULL on error
 * @see gvfs_archive_read_new, gvfs_archive_write_new, 
 *      gvfs_archive_readwrite_new, gvfs_archive_push_job, 
 *      gvfs_archive_free
 */
static GVfsArchive *
gvfs_archive_new (GVfsBackendArchive *ba, 
                  GVfsJob            *job,
                  gboolean            readable,
                  gboolean            writeable)
{
  GVfsArchive *d;
  int i;
  int result = ARCHIVE_OK;
  char *pathname;
  char *template;  
  
  g_assert (readable || writeable);
  
  d = g_slice_new0 (GVfsArchive);
  d->file = ba->file;
  d->stream = NULL;
  d->archive = NULL;
  d->temp_file = NULL;
  d->temp_stream = NULL;
  d->temp_archive = NULL;
  d->error = NULL;
  
  gvfs_archive_push_job (d, job);
    
  if (readable)
    {
      d->archive = archive_read_new ();
      archive_read_support_compression_all (d->archive);
      archive_read_support_format_all (d->archive);
      archive_read_set_seek_callback (d->archive, gvfs_archive_read_seek);
      result = archive_read_open2 (d->archive,
                                   d,
                                   gvfs_archive_read_open,
                                   gvfs_archive_read,
                                   gvfs_archive_read_skip,
                                   gvfs_archive_read_close);
      if (result < ARCHIVE_OK)
        {
          gvfs_archive_set_error_from_errno (d);
          
          return d;
        }
    }

  if (writeable)
    {
      /* Create a temp file. */
      pathname = g_file_get_path (ba->file);
      template = g_strconcat (pathname, ".XXXXXX", NULL);
      g_free (pathname);
      close (g_mkstemp (template));
      d->temp_file = g_file_new_for_path (template);
      g_free (template);
      
      /* Set up a format of the archive. */
      d->temp_archive = archive_write_new ();
      result = archive_write_set_format (d->temp_archive, ba->format);
      if (result != ARCHIVE_OK)
        {
          d->error = g_error_new_literal (G_IO_ERROR, 
                       G_IO_ERROR_FAILED, 
                       _("An archive format is not writeable."));
          return d;
        }
      
      /* Add filters of the archive. */
      for (i = 0; i < ba->filters_count; ++i)
        {
          result = archive_write_add_filter (d->temp_archive, ba->filters[i]);
          if (result != ARCHIVE_OK)
            {
              d->error = g_error_new_literal (G_IO_ERROR, 
                           G_IO_ERROR_FAILED,
                           _("An archive filter is not writeable."));
              return d;
            }
        }
      
      archive_write_set_bytes_in_last_block (d->temp_archive, 1);
      archive_write_set_options (d->temp_archive, "compression-level=9");
      
      result = archive_write_open (d->temp_archive,
                                   d,
                                   gvfs_archive_write_open,
                                   gvfs_archive_write,
                                   gvfs_archive_write_close);
      if (result < ARCHIVE_OK)
        {
          gvfs_archive_set_error_from_errno (d);
          
          return d;
        }
    }
    
  return d;
}

/**
 * Read next header from the archive.
 * @note Archive must be opened by ::gvfs_archive_new for reading.
 *       Filepath is without a leading slash.
 * @param archive ::GVfsArchive data structure.
 * @param entry Pointer to the archive entry (set by function).
 * @return ARCHIVE_OK on success, else LibArchive error code.
 * @see gvfs_archive_new, gvfs_archive_read_data
 */
static int 
gvfs_archive_read_header (GVfsArchive           *archive,
                          struct archive_entry **entry)
{
  int result;
  const char *pathname;
  
  if (gvfs_archive_in_error (archive))
    return ARCHIVE_FATAL;  
  
  result = archive_read_next_header (archive->archive, entry);
  if (result == ARCHIVE_OK)
    {
      /* Unificate a pathname to be without leading garbage. */
      pathname = archive_entry_pathname (*entry);
      if (g_str_has_prefix (pathname, "./"))
        {
          pathname += 2;
          archive_entry_set_pathname (*entry, pathname);
        }
    }
  else if (result < ARCHIVE_OK)
    gvfs_archive_set_error_from_errno (archive);
  
  return result;
}

/**
 * Write header to the archive.
 * @note Archive must be opened by ::gvfs_archive_new for writing.
 * @param archive ::GVfsArchive data structure.
 * @param entry Archive entry to write.
 * @return ARCHIVE_OK on success, else LibArchive error code.
 * @see gvfs_archive_new, gvfs_archive_write_data
 */
static int
gvfs_archive_write_header (GVfsArchive          *archive,
                           struct archive_entry *entry)
{
  int result;
  
  if (gvfs_archive_in_error (archive))
    return ARCHIVE_FATAL;
  
  result = archive_write_header (archive->temp_archive, entry);
  if (result < ARCHIVE_OK)
    gvfs_archive_set_error_from_errno (archive);
  
  return result;
}

/**
 * Read data blocks from the archive.
 * @note It must be used after ::gvfs_archive_read_header.
 *       Archive must be opened by ::gvfs_archive_new for reading.
 * @param archive ::GVfsArchive data structure.
 * @param data Buffer for reading.
 * @param size Size of the buffer.
 * @return Count of read bytes, ARCHIVE_FATAL on failure.
 * @see gvfs_archive_new, gvfs_archive_read_header, gvfs_archive_copy_data
 */
static ssize_t
gvfs_archive_read_data (GVfsArchive *archive,
                        guchar      *data,
                        size_t       size)
{
  ssize_t read_bytes;
  
  if (gvfs_archive_in_error (archive))
    return ARCHIVE_FATAL;

  read_bytes = archive_read_data (archive->archive, data, size);
  if (read_bytes < ARCHIVE_OK)
    gvfs_archive_set_error_from_errno (archive);
  
  return read_bytes;
}

/**
 * Write data blocks to the archive.
 * @note It must be used after ::gvfs_archive_write_header.
 *       Archive must be opened by ::gvfs_archive_new for writing.
 * @param archive ::GVfsArchive data structure.
 * @param data Buffer for writing.
 * @param size Size of the buffer.
 * @return Count of wrote bytes, ARCHIVE_FATAL on failure.
 * @see gvfs_archive_new, gvfs_archive_write_header, gvfs_archive_copy_data
 */
static ssize_t 
gvfs_archive_write_data (GVfsArchive *archive,
                         guchar      *data,
                         size_t       size)
{
  ssize_t write_bytes;
  
  if (gvfs_archive_in_error (archive))
    return ARCHIVE_FATAL;

  write_bytes = archive_write_data (archive->temp_archive, data, size);
  if (write_bytes < ARCHIVE_OK)
    gvfs_archive_set_error_from_errno (archive);
  
  return write_bytes;
}

/**
 * Copy data blocks from the read archive to the write archive.
 * @note Archive must be opened by ::gvfs_archive_new for reading and writing.
 * @param archive ::GVfsArchive data structure.
 * @see gvfs_archive_new, gvfs_archive_read_data, gvfs_archive_write_data
 */
static void 
gvfs_archive_copy_data (GVfsArchive *archive)
{
  ssize_t read_bytes;
  ssize_t write_bytes;
  guchar buffer[BLOCKSIZE];
  
  do
    {
      read_bytes = gvfs_archive_read_data (archive,
                                           buffer,
                                           sizeof (buffer));
      write_bytes = gvfs_archive_write_data (archive,
                                             buffer,
                                             read_bytes);
      if (read_bytes != write_bytes && !gvfs_archive_in_error (archive))
        archive->error = g_error_new_literal (G_IO_ERROR, 
                           G_IO_ERROR_FAILED, 
                           _("An archive entry size have not been set."));

      if (g_vfs_job_is_cancelled (archive->job) && 
          !gvfs_archive_in_error (archive))
        {
          archive->error = g_error_new_literal (G_IO_ERROR,
                                                G_IO_ERROR_CANCELLED,
                                                _("Operation was cancelled"));
          break;
        }
    }
  while (read_bytes > 0);
}

/**
 * Copy the archive until find an entry with a pathname prefix. 
 * @note Archive must be opened by ::gvfs_archive_new for reading and writing.
 *       Prefixes is absolute path, but must not start with a slash.
 * @param archive ::GVfsArchive data structure.
 * @param prefix1 Pathname prefix or NULL.
 * @param prefix2 Pathname prefix or NULL.
 * @return Archive entry if prefix was found, NULL on the end.
 * @see gvfs_archive_new, gvfs_archive_copy,
 *      gvfs_archive_read_data, gvfs_archive_write_data,
 *      gvfs_archive_read_header, gvfs_archive_write_header
 */
static struct archive_entry *
gvfs_archive_copy_prefix (GVfsArchive *archive,
                          const char  *prefix1,
                          const char  *prefix2)
{
  struct archive_entry *entry;
  const char *pathname;
  int result;
  int length;
  
  result = gvfs_archive_read_header (archive, &entry);
  while (result == ARCHIVE_OK)
    {
      pathname = archive_entry_pathname (entry);
      
      /* Check a prefix. */
      if (prefix1 != NULL && g_str_has_prefix (pathname, prefix1))
        {
          length = strlen(prefix1);
          if (pathname[length] == '\0' || 
              pathname[length] == '/')
            return entry;
        }
      if (prefix2 != NULL && g_str_has_prefix (pathname, prefix2))
        {
          length = strlen(prefix2);
          if (pathname[length] == '\0' || 
              pathname[length] == '/')
            return entry;
        }
      
      /* Write the header and data. */
      gvfs_archive_write_header (archive, entry);
      gvfs_archive_copy_data (archive);
           
      result = gvfs_archive_read_header (archive, &entry);
    }
  
  return NULL;
}

/**
 * Copy the whole archive. 
 * @note For more information look on ::gvfs_archive_copy_prefix.
 * @param archive ::GVfsArchive data structure.
 * @see gvfs_archive_new, gvfs_archive_copy_prefix,
 *      gvfs_archive_read_data, gvfs_archive_write_data, 
 *      gvfs_archive_read_header, gvfs_archive_write_header 
 */
#define gvfs_archive_copy(archive) \
  gvfs_archive_copy_prefix ((archive), NULL, NULL)


/*** BACKEND ***/

static void
g_vfs_backend_archive_finalize (GObject *object)
{
  GVfsBackendArchive *archive = G_VFS_BACKEND_ARCHIVE (object);

  backend_unmount (archive);

  if (G_OBJECT_CLASS (g_vfs_backend_archive_parent_class)->finalize)
    (*G_OBJECT_CLASS (g_vfs_backend_archive_parent_class)->finalize) (object);
}

static void
g_vfs_backend_archive_init (GVfsBackendArchive *archive)
{
}

/*** FILE TREE HANDLING ***/

/**
 * Find and optionally add a new file in the virtual file tree. 
 * @note Filename must not start with a slash. 
 *       The root of the file tree must be initialized by ::create_root_file.
 * @param file Initialized root ::ArchiveFile of the file tree.
 * @param filename Filename to search.
 * @param add Tell if a nonexistent filename will be added.
 * @return ::ArchiveFile if was found or added, NULL if was not found.
 * @see archive_file_find, create_root_file, archive_file_free,
 *      archive_file_set_info_from_entry, create_file_tree
 */
static ArchiveFile *
archive_file_get_from_path (ArchiveFile *file, const char *filename, gboolean add)
{
  char **names;
  ArchiveFile *cur;
  GSList *walk;
  guint i;

  /* libarchive reports paths starting with ./ for some archive types */
  if (g_str_has_prefix (filename, "./"))
    filename += 2;
  names = g_strsplit (filename, "/", -1);

  DEBUG ("%s %s\n", add ? "add" : "find", filename);
  for (i = 0; file && names[i] != NULL; i++)
    {
      cur = NULL;
      for (walk = file->children; walk; walk = walk->next)
	{
	  cur = walk->data;
	  if (g_str_equal (cur->name, names[i]))
	    break;
	  cur = NULL;
	}
      if (cur == NULL && add != FALSE)
	{
	  DEBUG ("adding node %s to %s\n", names[i], file->name);
	  /* (hopefully) clever trick to avoid string copy */
	  if (names[i][0] != 0 &&
              strcmp (names[i], ".") != 0)
	    {
	      cur = g_slice_new0 (ArchiveFile);
	      cur->name = names[i];
	      names[i] = NULL;
	      file->children = g_slist_prepend (file->children, cur);
              cur->parent = file;
	    }
	  else
	    {
	      /* happens when adding directories, their path ends with a / */
              /* Can also happen with "." in e.g. iso files */
	      g_assert (names[i + 1] == NULL);
	      g_free (names[i]);
	      names[i] = NULL;
	      cur = file;
	    }
	}
      file = cur;
    }
  g_strfreev (names);
  return file;
}

/**
 * Find a file in the virtual file tree. 
 * @note Filename must start with a slash.
 *       For more information look on ::archive_file_get_from_path.     
 * @param ba Structure ::GVfsBackendArchive.
 * @param filename Filename to search.
 * @return ::ArchiveFile if was found, NULL if was not found.
 * @see archive_file_get_from_path, create_root_file
 */
#define archive_file_find(ba, filename) archive_file_get_from_path((ba)->files, (filename) + 1, FALSE)

/**
 * Create a root file of the virtual file tree. 
 * @param ba Structure ::GVfsBackendArchive.
 * @see archive_file_get_from_path, archive_file_find, 
 *      create_file_tree, archive_file_free
 */
static void
create_root_file (GVfsBackendArchive *ba)
{
  ArchiveFile *root;
  GFileInfo *info;
  char *s, *display_name;
  GIcon *icon;

  root = g_slice_new0 (ArchiveFile);
  root->name = g_strdup ("/");
  root->parent = NULL;
  ba->files = root;

  info = g_file_info_new ();
  root->info = info;

  g_file_info_set_file_type (info, G_FILE_TYPE_DIRECTORY);

  g_file_info_set_name (info, "/");
  s = g_file_get_basename (ba->file);
  /* FIXME: this should really be "/ in %s", but can't change
     due to string freeze. */
  display_name = g_strdup_printf (_("/ on %s"), s);
  g_free (s);
  g_file_info_set_display_name (info, display_name);
  g_free (display_name);
  g_file_info_set_edit_name (info, "/");

  g_file_info_set_content_type (info, "inode/directory");
  g_file_info_set_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE, "inode/directory");

  g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ, TRUE);
  g_file_info_set_attribute_boolean (info, 
                                     G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, 
                                     ba->writable);
  g_file_info_set_attribute_boolean (info, 
                                     G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE, 
                                     ba->writable);
  g_file_info_set_attribute_boolean (info, 
                                     G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE, 
                                     ba->writable);
  g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH, FALSE);
  g_file_info_set_attribute_boolean (info, 
                                     G_FILE_ATTRIBUTE_ACCESS_CAN_RENAME, 
                                     ba->writable);

  icon = g_themed_icon_new ("folder");
  g_file_info_set_icon (info, icon);
  g_object_unref (icon);
}

/**
 * Set ::ArchiveFile info from the archive entry info. 
 * @param ba Structure ::GVfsBackendArchive.
 * @param file Initialized ::ArchiveFile for setting the info.
 * @param entry Archive entry containing info.
 * @param entry_index Order number in the archive.
 * @see archive_file_get_from_path, archive_entry_set_info, create_file_tree
 */
static void
archive_file_set_info_from_entry (GVfsBackendArchive   *ba,
                                  ArchiveFile *         file, 
				  struct archive_entry *entry,
				  guint64               entry_index)
{
  GFileInfo *info = g_file_info_new ();
  GFileType type;
  file->info = info;

  DEBUG ("setting up %s (%s)\n", archive_entry_pathname (entry), file->name);

  g_file_info_set_attribute_uint64 (info,
				    G_FILE_ATTRIBUTE_TIME_ACCESS,
				    archive_entry_atime (entry));
  g_file_info_set_attribute_uint32 (info,
				    G_FILE_ATTRIBUTE_TIME_ACCESS_USEC,
				    archive_entry_atime_nsec (entry) / 1000);
  g_file_info_set_attribute_uint64 (info,
				    G_FILE_ATTRIBUTE_TIME_CHANGED,
				    archive_entry_ctime (entry));
  g_file_info_set_attribute_uint32 (info,
				    G_FILE_ATTRIBUTE_TIME_CHANGED_USEC,
				    archive_entry_ctime_nsec (entry) / 1000);
  g_file_info_set_attribute_uint64 (info,
				    G_FILE_ATTRIBUTE_TIME_MODIFIED,
				    archive_entry_mtime (entry));
  g_file_info_set_attribute_uint32 (info,
				    G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC,
				    archive_entry_mtime_nsec (entry) / 1000);

  switch (archive_entry_filetype (entry))
    {
      case AE_IFREG:
	type = G_FILE_TYPE_REGULAR;
	break;
      case AE_IFLNK:
	g_file_info_set_symlink_target (info,
	                                archive_entry_symlink (entry));
	type = G_FILE_TYPE_SYMBOLIC_LINK;
	break;
      case AE_IFDIR:
	type = G_FILE_TYPE_DIRECTORY;
	break;
      case AE_IFCHR:
      case AE_IFBLK:
      case AE_IFIFO:
      case AE_IFSOCK:
      case AE_IFMT:
	type = G_FILE_TYPE_SPECIAL;
	break;
      default:
	g_warning ("unknown file type %u", archive_entry_filetype (entry));
	type = G_FILE_TYPE_SPECIAL;
	break;
    }
  g_file_info_set_name (info, file->name);
  gvfs_file_info_populate_default (info,
				   file->name,
				   type);

  /* Set size if it is known. */
  if (archive_entry_size_is_set (entry))
    g_file_info_set_size (info,
                          archive_entry_size (entry));

  g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ, TRUE);
  g_file_info_set_attribute_boolean (info, 
                                     G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE, 
                                     ba->writable);
  g_file_info_set_attribute_boolean (info, 
                                     G_FILE_ATTRIBUTE_ACCESS_CAN_DELETE, 
                                     ba->writable);
  g_file_info_set_attribute_boolean (info, 
                                     G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE, 
                                     ba->writable);
  g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH, FALSE);
  g_file_info_set_attribute_boolean (info, 
                                     G_FILE_ATTRIBUTE_ACCESS_CAN_RENAME, 
                                     ba->writable);

  /* Set inode number to reflect absolute position in the archive. */
  g_file_info_set_attribute_uint64 (info,
				    G_FILE_ATTRIBUTE_UNIX_INODE,
				    entry_index);


  /* FIXME: add info for these
dev_t			 archive_entry_dev(struct archive_entry *);
dev_t			 archive_entry_devmajor(struct archive_entry *);
dev_t			 archive_entry_devminor(struct archive_entry *);
void			 archive_entry_fflags(struct archive_entry *,
			     unsigned long *set, unsigned long *clear);
const char		*archive_entry_fflags_text(struct archive_entry *);
gid_t			 archive_entry_gid(struct archive_entry *);
const char		*archive_entry_gname(struct archive_entry *);
const char		*archive_entry_hardlink(struct archive_entry *);
mode_t			 archive_entry_mode(struct archive_entry *);
unsigned int		 archive_entry_nlink(struct archive_entry *);
dev_t			 archive_entry_rdev(struct archive_entry *);
dev_t			 archive_entry_rdevmajor(struct archive_entry *);
dev_t			 archive_entry_rdevminor(struct archive_entry *);
uid_t			 archive_entry_uid(struct archive_entry *);
const char		*archive_entry_uname(struct archive_entry *);
  */

  /* FIXME: do ACLs */
}

/**
 * Set archive entry info from file info. 
 * @note Pathname must not start with a slash.
 * @param entry Initialized archive entry for setting.
 * @param pathname Name of the new file.
 * @param info File info.
 * @see archive_file_set_info_from_entry
 */
static void
archive_entry_set_info (struct archive_entry *entry,
                        const char           *pathname,
                        GFileInfo            *info)
{
  guint type;

  /* Set up times. */
  archive_entry_set_birthtime (entry,
    g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_CREATED),
    g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_CREATED_USEC) 
      * 1000);
  archive_entry_set_atime (entry,
    g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_ACCESS),
    g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_ACCESS_USEC) 
      * 1000);
  archive_entry_set_ctime (entry,
    g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_CHANGED),
    g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_CHANGED_USEC) 
      * 1000);
  archive_entry_set_mtime (entry,
    g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED),
    g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC) 
      * 1000);
  
  /* Set up a type. */
  switch (g_file_info_get_file_type (info))
    {
      case G_FILE_TYPE_REGULAR:
        type = AE_IFREG;
        break;
      case G_FILE_TYPE_SYMBOLIC_LINK:
        archive_entry_set_symlink (entry, 
                                   g_file_info_get_symlink_target (info) + 1);
        type = AE_IFLNK;
        break;
      case G_FILE_TYPE_DIRECTORY:
        type = AE_IFDIR;
        break;
      case G_FILE_TYPE_SPECIAL:
        /* Set up a type of the special type. */
        switch (g_file_info_get_attribute_uint32 (info, 
                                                  G_FILE_ATTRIBUTE_UNIX_MODE))
          {
            case S_IFCHR:
              type = AE_IFCHR;
              break;
            case S_IFBLK:
              type = AE_IFBLK;
              break;
            case S_IFIFO:
              type = AE_IFIFO;
              break;
            case S_IFSOCK:
              type = AE_IFSOCK;
              break;
            case S_IFMT:
              type = AE_IFMT;
              break;
            default:
              g_warning ("Unknown file mode");
              type = AE_IFREG;
              break;
          }
        break;
      default:
        g_warning ("Unknown file type");
        type = AE_IFREG;
        break;
    }
  
  archive_entry_set_filetype (entry, type);
  archive_entry_set_pathname (entry, pathname);
  archive_entry_set_size (entry, g_file_info_get_size (info));
  archive_entry_set_perm (entry, 0644);

  /* FIXME: Add additional info.
     void archive_entry_set_dev(struct archive_entry *, dev_t);
     void archive_entry_set_devmajor(struct archive_entry *, dev_t);
     void archive_entry_set_devminor(struct archive_entry *, dev_t);
     void archive_entry_set_fflags(struct archive_entry *,
     void archive_entry_set_gid(struct archive_entry *, __LA_INT64_T);
     void archive_entry_set_gname(struct archive_entry *, const char *);
     void archive_entry_set_hardlink(struct archive_entry *, const char *);
     void archive_entry_set_ino64(struct archive_entry *, __LA_INT64_T);
     void archive_entry_set_link(struct archive_entry *, const char *);
     void archive_entry_set_mode(struct archive_entry *, __LA_MODE_T);
     void archive_entry_set_nlink(struct archive_entry *, unsigned int);
     void archive_entry_set_rdev(struct archive_entry *, dev_t);
     void archive_entry_set_rdevmajor(struct archive_entry *, dev_t);
     void archive_entry_set_rdevminor(struct archive_entry *, dev_t);
     void archive_entry_set_uid(struct archive_entry *, __LA_INT64_T);
     void archive_entry_set_uname(struct archive_entry *, const char *);
  */
}

/**
 * Set default dir info if was not set from archive entry. 
 * @param file Initialized root ::ArchiveFile of the file tree.
 * @see create_file_tree, create_root_file, archive_file_get_from_path
 */
static void
fixup_dirs (ArchiveFile *file)
{
  GSList *l;

  if (file->info == NULL)
    {
      GFileInfo *info = g_file_info_new ();
      
      file->info = info;
      g_file_info_set_name (info, file->name);
      gvfs_file_info_populate_default (info,
                                       file->name,
                                       G_FILE_TYPE_DIRECTORY);
    }
  
  for (l = file->children; l != NULL; l = l->next)
    fixup_dirs (l->data);
}

/**
 * Create virtual file tree from the archive.
 * @note The GVFS job is popped by ::gvfs_archive_pop_job.
 * @param ba Initialized ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @see fixup_dirs, create_root_file, archive_file_free,
 *      archive_file_get_from_path, archive_file_set_info_from_entry
 */
static void
create_file_tree (GVfsBackendArchive *ba, GVfsJob *job)
{
  GVfsArchive *archive;
  struct archive_entry *entry;
  int result;
  guint64 entry_index = 0;

  archive = gvfs_archive_read_new (ba, job);

  g_assert (ba->files != NULL);

  do
    {
      result = archive_read_next_header (archive->archive, &entry);
      if (result >= ARCHIVE_WARN && result <= ARCHIVE_OK)
	{
  	  if (result < ARCHIVE_OK) {
  	    DEBUG ("archive_read_next_header: result = %d, error = '%s'\n", result, archive_error_string (archive->archive));
  	    archive_set_error (archive->archive, ARCHIVE_OK, "No error");
  	    archive_clear_error (archive->archive);
	  }
  
	  ArchiveFile *file = archive_file_get_from_path (ba->files, 
	                                                  archive_entry_pathname (entry), 
							  TRUE);
          /* Don't set info for root */
          if (file != ba->files)
            archive_file_set_info_from_entry (ba, file, entry, entry_index);
	  archive_read_data_skip (archive->archive);
	  entry_index++;
	}
    }
  while (result != ARCHIVE_FATAL && result != ARCHIVE_EOF);

  if (result == ARCHIVE_FATAL)
    gvfs_archive_set_error_from_errno (archive);
  fixup_dirs (ba->files);
  
  gvfs_archive_finish (archive);
}

/**
 * Free the virtual file tree.
 * @note The GVFS job is popped by ::gvfs_archive_pop_job.
 * @param file Initialized root ::ArchiveFile of the file tree.
 * @see create_root_file, create_file_tree, archive_file_get_from_path
 */
static void
archive_file_free (ArchiveFile *file)
{
  g_slist_foreach (file->children, (GFunc) archive_file_free, NULL);
  g_slist_free (file->children);
  if (file->info)
    g_object_unref (file->info);
  g_free (file->name);
}

/* Check whether the file is an archive and determine a format. */
static GError *
determine_archive_format (GVfsBackendArchive *ba, 
                          GVfsJob            *job)
{
  int i;
  GVfsArchive *archive;
  GError *error;
  struct archive_entry *entry;
  int result;
  
  archive = gvfs_archive_read_new (ba, job);
  result = gvfs_archive_read_header (archive, &entry);
  if (result == ARCHIVE_FATAL)
    {
      error = g_error_new_literal (G_IO_ERROR, 
                g_io_error_from_errno (archive_errno (archive->archive)), 
                archive_error_string (archive->archive));
      gvfs_archive_free (archive, FALSE);
      
      return error;
    }
  
  DEBUG ("determine format %s (%d)\n", 
         archive_format_name (archive->archive),
         archive_format (archive->archive));
  
  ba->format = archive_format (archive->archive);
  ba->filters_count = archive_filter_count (archive->archive);
  ba->filters = g_slice_alloc (
                  archive_filter_count (archive->archive) * sizeof (int));
  for (i = 0; i < ba->filters_count; ++i)
  {
    ba->filters[i] = archive_filter_code (archive->archive, i);
    
    DEBUG ("determine filter %s (%d)\n", 
           archive_filter_name (archive->archive, i),  
           ba->filters[i]);
  }

  /* FIXME: Determine an archive options too. */
  
  gvfs_archive_free (archive, FALSE);
  
  /* Check if format is writable. */
  archive->archive = archive_write_new ();
  result = archive_write_set_format (archive->archive, ba->format);
  for (i = 0; i < ba->filters_count && result == ARCHIVE_OK; i++)
    result = archive_write_add_filter (archive->archive, ba->filters [i]);
  archive_write_free (archive->archive);
  
  if (result != ARCHIVE_OK)
    ba->writable = FALSE;
  
  if (ba->format == ARCHIVE_FORMAT_EMPTY)
    return g_error_new_literal (G_IO_ERROR, 
                                G_IO_ERROR_NOT_MOUNTABLE_FILE,
                                _("Invalid file"));
  
  return NULL;
}

/**
 * Create an empty archive file.
 * @note The GVFS job is popped by ::gvfs_archive_pop_job.
 * @param ba Initialized ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @see do_mount
 */
static void
create_empty_archive (GVfsBackendArchive *ba, 
                      GVfsJob            *job)
{
  GVfsArchive *archive;
  GFileOutputStream *stream;
  
  /* Create an empty file. */
  stream = g_file_create (ba->file, 
                          G_FILE_CREATE_NONE, 
                          job->cancellable, 
                          &job->error);
  if (stream == NULL)
    {
      g_vfs_job_failed_from_error (G_VFS_JOB (job),
                                   job->error);
      return;
    }
  else
    g_object_unref (stream);
  
  /* Create an empty archive. */
  archive = gvfs_archive_write_new (ba, job);
  if (gvfs_archive_in_error (archive))
    {
      g_file_delete (archive->file, 
                     NULL, 
                     NULL);
    }
  
  gvfs_archive_finish (archive);
}

/**
 * Mount the archive backend.
 * @note The GVFS operation.
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param mount_spec Backend mount options.
 * @param mount_source Backend connection handler.
 * @param is_automount Tell if is mounted automaticaly.
 * @see create_empty_archive, determine_archive_format, 
 *      create_file_tree, do_unmount
 */
static void
do_mount (GVfsBackend *backend,
	  GVfsJobMount *job,
	  GMountSpec *mount_spec,
	  GMountSource *mount_source,
	  gboolean is_automount)
{
  GVfsBackendArchive *archive = G_VFS_BACKEND_ARCHIVE (backend);
  GError *error;
  const char *host, *file;
  const char *create;
  const char *format;
  const char *filters;
  char *filename, *s;
  char *endptr;

  host = g_mount_spec_get (mount_spec, "host");
  file = g_mount_spec_get (mount_spec, "file");
  if (host == NULL &&
      file == NULL)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
                       G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                       _("No hostname specified"));
      return;
    }

  if (host != NULL)
    {
      filename = g_uri_unescape_string (host, NULL);
      if (filename == NULL)
        {
          g_vfs_job_failed (G_VFS_JOB (job),
                            G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            _("Invalid mount spec"));
          return;
        }
      
      archive->file = g_file_new_for_commandline_arg (filename);
      g_free (filename);
    }
  else
    archive->file = g_file_new_for_commandline_arg (file);
  
  DEBUG ("Trying to mount %s\n", g_file_get_uri (archive->file));
  
  archive->writable = TRUE;
  archive->format = ARCHIVE_FORMAT_EMPTY;
  archive->filters_count = 0;
  archive->filters = NULL;

  create = g_mount_spec_get (mount_spec, "create");
  format = g_mount_spec_get (mount_spec, "format");
  filters = g_mount_spec_get (mount_spec, "filters");
  if (create != NULL)
    {
      if (format == NULL)
        {
           g_vfs_job_failed (G_VFS_JOB (job),
                             G_IO_ERROR,
                             G_IO_ERROR_INVALID_ARGUMENT,
                             _("No format specified"));
           return;
        }
      
      /* Read a format for the archive. */
      archive->format = strtol (format, &endptr, 10);
      if (*endptr != '\0')
        {
          g_vfs_job_failed (G_VFS_JOB (job),
                            G_IO_ERROR, 
                            G_IO_ERROR_INVALID_ARGUMENT,
                            _("Invalid format"));
          return;
        }
      
      if (filters != NULL)
        {
          /* Read filters for the new archive. */
          archive->filters_count = 0;
          while (*filters != '\0')
            {
              archive->filters_count++;
              archive->filters = g_realloc (archive->filters, 
                                            archive->filters_count
                                              * sizeof (int));
              archive->filters [archive->filters_count - 1] = strtol (filters, 
                                                                      &endptr, 
                                                                      10);
              filters = endptr;
              if (*filters != '\0')
                {
                  if (*filters != ',')
                    {
                      g_vfs_job_failed (G_VFS_JOB (job),
                                        G_IO_ERROR, 
                                        G_IO_ERROR_INVALID_ARGUMENT,
                                        _("Invalid filter"));
                      g_free (archive->filters);
                      
                      return;
                    }
                  else
                    filters++;
                }
            }
        }
    }
  else
    {
      /* Check whether the file is an archive and determine a format. */
      error = determine_archive_format (archive, G_VFS_JOB (job));
      if (error != NULL)
        {
          g_vfs_job_failed_from_error (G_VFS_JOB (job), error);
          g_clear_error (&error);
          
          return;
        }
    }
  
  archive->write_lock = g_mutex_new ();
  archive->read_lock = g_mutex_new ();
  
  filename = g_file_get_uri (archive->file);
  DEBUG ("mounted %s\n", filename);
  s = g_uri_escape_string (filename, NULL, FALSE);
  mount_spec = g_mount_spec_new ("archive");
  g_mount_spec_set (mount_spec, "host", s);
  g_free (s);
  g_vfs_backend_set_mount_spec (backend, mount_spec);
  g_mount_spec_unref (mount_spec);
  
  s = g_path_get_basename (filename);
  g_vfs_backend_set_display_name (backend, s);
  g_free (s);

  g_vfs_backend_set_icon_name (backend, MOUNT_ICON_NAME);

  create_root_file (archive);
  if (create == NULL)
    {
      create_file_tree (archive, G_VFS_JOB (job));
    }
  else
    create_empty_archive (archive, G_VFS_JOB (job));
}

/**
 * Free ::GVfsBackendArchive members.
 * @param ba ::GVfsBackendArchive structure.
 * @see do_unmount
 */
static void
backend_unmount (GVfsBackendArchive *ba)
{
  if (ba->file)
    {
      g_object_unref (ba->file);
      ba->file = NULL;
    }
  if (ba->files)
    {
      archive_file_free (ba->files);
      ba->files = NULL;
    }
  if (ba->filters)
    {
      g_slice_free1 (ba->filters_count * sizeof (int), ba->filters);
      ba->filters = NULL;
    }
  if (ba->read_lock)
    {
      g_mutex_free (ba->read_lock);
      ba->read_lock = NULL;
    }
  if (ba->write_lock)
    {
      g_mutex_free (ba->write_lock);
      ba->write_lock = NULL;
    }
}

/**
 * Unmount the archive backend.
 * @note The GVFS operation.
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param flags Backend unmount options.
 * @param mount_source Backend connection handler.
 * @see do_mount, backend_unmount
 */
static void
do_unmount (GVfsBackend *backend,
	    GVfsJobUnmount *job,
            GMountUnmountFlags flags,
            GMountSource *mount_source)
{
  backend_unmount (G_VFS_BACKEND_ARCHIVE (backend));

  g_vfs_job_succeeded (G_VFS_JOB (job));
}

/**
 * Open archive for reading.
 * @note The GVFS operation.
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param filename File pathname to read.
 * @see do_read, do_close_read
 */
static void
do_open_for_read (GVfsBackend *       backend,
		  GVfsJobOpenForRead *job,
		  const char *        filename)
{
  GVfsBackendArchive *ba = G_VFS_BACKEND_ARCHIVE (backend);
  GVfsArchive *archive;
  struct archive_entry *entry;
  int result;
  ArchiveFile *file;
  const char *entry_pathname;

  g_mutex_lock (ba->read_lock);
  
  file = archive_file_find (ba, filename);
  if (file == NULL)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
		        G_IO_ERROR,
			G_IO_ERROR_NOT_FOUND,
			_("File doesn't exist"));
      g_mutex_unlock (ba->read_lock);
      
      return;
    }

  if (g_file_info_get_file_type (file->info) == G_FILE_TYPE_DIRECTORY)
    {
      g_vfs_job_failed (G_VFS_JOB (job), G_IO_ERROR,
			G_IO_ERROR_IS_DIRECTORY,
			_("Can't open directory"));
      g_mutex_unlock (ba->read_lock);
      
      return;
    }
  
  g_mutex_unlock (ba->read_lock);
  
  archive = gvfs_archive_read_new (ba, G_VFS_JOB (job));

  do
    {
      result = archive_read_next_header (archive->archive, &entry);
      if (result >= ARCHIVE_WARN && result <= ARCHIVE_OK)
        {
	  if (result < ARCHIVE_OK) {
	    DEBUG ("do_open_for_read: result = %d, error = '%s'\n", result, archive_error_string (archive->archive));
	    archive_set_error (archive->archive, ARCHIVE_OK, "No error");
	    archive_clear_error (archive->archive);
	  }

          entry_pathname = archive_entry_pathname (entry);
          /* skip leading garbage if present */
          if (g_str_has_prefix (entry_pathname, "./"))
            entry_pathname += 2;
          if (g_str_equal (entry_pathname, filename + 1))
            {
              /* SUCCESS */
              g_vfs_job_open_for_read_set_handle (job, archive);
              g_vfs_job_open_for_read_set_can_seek (job, FALSE);
              gvfs_archive_pop_job (archive);
              return;
            }
          else
            archive_read_data_skip (archive->archive);
        }
    }
  while (result != ARCHIVE_FATAL && result != ARCHIVE_EOF);

  if (!gvfs_archive_in_error (archive))
    {
      g_set_error_literal (&archive->error,
			   G_IO_ERROR,
			   G_IO_ERROR_NOT_FOUND,
			   _("File doesn't exist"));
    }

  gvfs_archive_finish (archive);
}

/**
 * Close archive after reading.
 * @note The GVFS operation.
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param handle Opened ::GVfsArchive structure.
 * @see do_read, do_open_for_read
 */
static void
do_close_read (GVfsBackend *backend,
	       GVfsJobCloseRead *job,
	       GVfsBackendHandle handle)
{
  GVfsArchive *archive = handle;

  gvfs_archive_push_job (archive, G_VFS_JOB (job));
  gvfs_archive_finish (archive);
}

/**
 * Read a data from the archive.
 * @note The GVFS operation.
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param handle Opened ::GVfsArchive structure.
 * @param buffer Buffer for read data.
 * @param bytes_requested Count of bytes to read.
 * @see do_close_read, do_open_for_read
 */
static void
do_read (GVfsBackend *backend,
	 GVfsJobRead *job,
	 GVfsBackendHandle handle,
	 char *buffer,
	 gsize bytes_requested)
{
  GVfsArchive *archive = handle;
  gssize bytes_read;

  gvfs_archive_push_job (archive, G_VFS_JOB (job));
  bytes_read = archive_read_data (archive->archive, buffer, bytes_requested);
  if (bytes_read >= 0)
    g_vfs_job_read_set_size (job, bytes_read);
  else
    gvfs_archive_set_error_from_errno (archive);
  gvfs_archive_pop_job (archive);
}

/**
 * Push the file from the local filesystem in to the archive.
 * @note The GVFS operation.
 *       For details about flags and structures see GFile documentation page. 
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param destination Destination pathname for file in the archive.
 * @param source Source pathname for the local filesystem file.
 * @param flags Copy flags.
 * @param remove_source TRUE for move, FALSE for copy.
 * @param progress_callback Progress callback.
 * @param progress_callback_data Data for progress callback.
 */
static void
do_push (GVfsBackend          *backend,
         GVfsJobPush          *job,
         const char           *destination,
         const char           *source,
         GFileCopyFlags        flags,
         gboolean              remove_source,
         GFileProgressCallback progress_callback,
         gpointer              progress_callback_data)
{
  GVfsBackendArchive *ba = G_VFS_BACKEND_ARCHIVE (backend);
  GVfsArchive *archive;
  ArchiveFile *archive_file;
  struct archive_entry *entry;
  GFile *file;
  GFileInfo *info;
  GFileType type;
  GFileInputStream *stream;
  gboolean is_dir;
  ssize_t read_bytes;
  ssize_t write_bytes;
  ssize_t size;
  ssize_t copied;
  
  DEBUG ("push %s to %s\n", source, destination);
  
  /* Lock backend for write. */
  if (!g_mutex_trylock (ba->write_lock))
    {
      g_vfs_job_failed (G_VFS_JOB (job), 
                        G_IO_ERROR,
                        G_IO_ERROR_BUSY,
                        _("Can't do multiple write operations"));
      return;
    }
  
  /* Check for possible errors. */
  is_dir = g_file_test (source, G_FILE_TEST_IS_DIR);
  archive_file = archive_file_find (ba, destination);
  if (archive_file != NULL)
    {
      if (flags & G_FILE_COPY_OVERWRITE)
        {
          if (is_dir)
            {
              type = g_file_info_get_file_type (archive_file->info);
              if (type == G_FILE_TYPE_DIRECTORY)
                {
                  g_vfs_job_failed (G_VFS_JOB (job), 
                                    G_IO_ERROR,
                                    G_IO_ERROR_WOULD_MERGE,
                                   _("Can't copy directory over directory"));
                  g_mutex_unlock (ba->write_lock);
                  
                  return;
                }
              else
                {
                  g_vfs_job_failed (G_VFS_JOB (job), 
                                    G_IO_ERROR,
                                    G_IO_ERROR_WOULD_RECURSE,
                                    _("Can't recursively copy directory"));
                  g_mutex_unlock (ba->write_lock);
                  
                  return;
                }
            }
        }
      else
        {
          g_vfs_job_failed (G_VFS_JOB (job), 
                            G_IO_ERROR,
                            G_IO_ERROR_EXISTS,
                            _("Target file already exists"));
          g_mutex_unlock (ba->write_lock);
          
          return;
        }
    }
  
  if (is_dir)
    {
      g_vfs_job_failed (G_VFS_JOB (job), 
                        G_IO_ERROR,
                        G_IO_ERROR_WOULD_RECURSE,
                        _("Can't recursively copy directory"));
      g_mutex_unlock (ba->write_lock);
      
      return;
    }
  
  if (!(flags & G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS))
    g_warning ("FIXME: follow symlinks");
  
  /* Copy the whole archive except the overwritten file. */
  archive = gvfs_archive_readwrite_new (ba, G_VFS_JOB (job));
  entry = gvfs_archive_copy_prefix (archive, destination + 1, NULL);
  while (entry != NULL)
    {
      archive_read_data_skip (archive->archive);
      
      entry = gvfs_archive_copy_prefix (archive, destination + 1, NULL);
    }
  
  if (gvfs_archive_in_error (archive))
    {
      gvfs_archive_finish (archive);
      g_mutex_unlock (ba->write_lock);
      
      return;
    }
  
  /* Add the new file into the archive. */
  file = g_file_new_for_path (source);
  info = g_file_query_info (file,
                            "*",
                            G_FILE_QUERY_INFO_NONE,
                            archive->job->cancellable,
                            &archive->error);
  if (gvfs_archive_in_error (archive))
    {
      g_object_unref (file);
      gvfs_archive_finish (archive);
      g_mutex_unlock (ba->write_lock);
      
      return;
    }
  
  stream = g_file_read (file, G_VFS_JOB (job)->cancellable, &archive->error);  
  if (gvfs_archive_in_error (archive))
    {
      g_object_unref (file);
      gvfs_archive_finish (archive);
      g_mutex_unlock (ba->write_lock);
      
      return;
    }
  
  entry = archive_entry_new ();
  archive_entry_set_info (entry, destination + 1, info);
  gvfs_archive_write_header (archive, entry);
  
  size = archive_entry_size (entry);
  copied = 0;
  if (progress_callback != NULL)
    progress_callback (copied, size, progress_callback_data);
  do
    {
      read_bytes = g_input_stream_read (G_INPUT_STREAM (stream),
                                        archive->data,
                                        sizeof (archive->data),
                                        archive->job->cancellable,
                                        &archive->error);
      write_bytes = gvfs_archive_write_data (archive, 
                                             archive->data, 
                                             read_bytes);
      
      copied += read_bytes;
      if (progress_callback != NULL)
        progress_callback (copied, size, progress_callback_data);
      
      if (g_vfs_job_is_cancelled (archive->job) && 
          !gvfs_archive_in_error (archive))
        {
          archive->error = g_error_new_literal (G_IO_ERROR,
                                                G_IO_ERROR_CANCELLED,
                                                _("Operation was cancelled"));
          break;
        }
    }
  while (read_bytes > 0);
  
  if (!gvfs_archive_in_error (archive))
    {
      /* Add the new file into the file tree. */
      g_mutex_lock (ba->read_lock);
      
      archive_file = archive_file_get_from_path (ba->files, 
                                                 destination + 1, 
                                                 TRUE);
      if (archive_file->info != NULL)
        g_object_unref (archive_file->info);
      archive_file->info = info;
      
      g_mutex_unlock (ba->read_lock);
      
      /* Remove source file if necessary. */
      if (remove_source)
        g_file_delete (file,
                       archive->job->cancellable,
                       &archive->error);
    }
  
  g_mutex_unlock (ba->write_lock);
  archive_entry_free (entry);
  g_object_unref (stream);
  g_object_unref (file);  
  gvfs_archive_finish (archive);
}

/**
 * Rename file inside the archive.
 * @note The GVFS operation.
 *       For move use ::do_move.
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param pathname Source file pathname.
 * @param display_name New name for file without path.
 * @see do_move
 */
static void
do_set_display_name (GVfsBackend           *backend,
                     GVfsJobSetDisplayName *job,
                     const char            *pathname,
                     const char            *display_name)
{
  GVfsBackendArchive *ba = G_VFS_BACKEND_ARCHIVE (backend);
  GVfsArchive *archive;
  struct archive_entry *entry;
  ArchiveFile *file;
  const char *pathname_entry;
  char *pathname_new;
  char *name;
  
  DEBUG ("rename %s to %s\n", pathname, display_name);
  
  /* Lock backend for write. */
  if (!g_mutex_trylock (ba->write_lock))
    {
      g_vfs_job_failed (G_VFS_JOB (job), 
                        G_IO_ERROR,
                        G_IO_ERROR_BUSY,
                        _("Can't do multiple write operations"));
      return;
    }
  
  /* Check validity of the file name. */
  if (g_strrstr (display_name, "/") != NULL || strlen (display_name) == 0)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
                        G_IO_ERROR,
                        G_IO_ERROR_INVALID_ARGUMENT,
                        _("Filename is invalid"));
      g_mutex_unlock (ba->write_lock);
      
      return;
    }
  
  if (strcmp (pathname, "/") == 0)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));
      g_mutex_unlock (ba->write_lock);
      
      return;
    }
  
  /* Check whether the source file exists. */
  file = archive_file_find (ba, pathname);
  if (file == NULL)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_FOUND,
                        _("File doesn't exist"));
      g_mutex_unlock (ba->write_lock);
      
      return;
    }
  
  /* Create a new pathname. */
  name = g_path_get_dirname (pathname);
  pathname_new = g_build_filename (name, display_name, NULL);
  g_free (name);
  
  /* Pathnames are equal. */
  if (strcmp (pathname, pathname_new) == 0)
    {
      g_vfs_job_set_display_name_set_new_path (job, pathname_new);
      g_free (pathname_new);
      g_mutex_unlock (ba->write_lock);
      g_vfs_job_succeeded (G_VFS_JOB (job));
      
      return;
    }
  
  /* Check whether the destination file does not exists. */
  if (archive_file_find (ba, pathname_new) != NULL)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
                        G_IO_ERROR,
                        G_IO_ERROR_EXISTS,
                        _("Target file already exists"));
      g_free (pathname_new);
      g_mutex_unlock (ba->write_lock);
      
      return; 
    }
  
  /* Change the name in the archive file. */
  archive = gvfs_archive_readwrite_new (ba, G_VFS_JOB (job));
  entry = gvfs_archive_copy_prefix (archive, pathname + 1, NULL);
  while (entry != NULL)
    {
      pathname_entry = archive_entry_pathname (entry);
      
      /* Change the name in the archive entry. */
      name = g_build_filename (pathname_new + 1, 
                               pathname_entry + strlen (pathname) - 1, 
                               NULL);
      archive_entry_set_pathname (entry, name);
      g_free (name);

      /* Write the header and data. */
      gvfs_archive_write_header (archive, entry);
      gvfs_archive_copy_data (archive);
      
      entry = gvfs_archive_copy_prefix (archive, pathname + 1, NULL);
    }
  
  if (!gvfs_archive_in_error (archive))
    {
      /* Change the name in the file tree. */
      g_mutex_lock (ba->read_lock);
      
      g_free (file->name);
      file->name = g_strdup (display_name);
      g_file_info_set_name (file->info, display_name);
      gvfs_file_info_populate_default (file->info,
                                       file->name,
                                       g_file_info_get_file_type (file->info));
      g_vfs_job_set_display_name_set_new_path (job, pathname_new);
      
      g_mutex_unlock (ba->read_lock);
    }
  
  g_mutex_unlock (ba->write_lock);
  g_free (pathname_new);
  gvfs_archive_finish (archive);  
}

/**
 * Move file inside the archive.
 * @note The GVFS operation.
 *       For rename use ::do_set_display_name.
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param source Source file pathname.
 * @param destination Destination file pathname.
 * @param flags Copy flags.
 * @param progress_callback Progress callback.
 * @param progress_callback_data Data for progress callback.
 * @see do_set_display_name
 */
static void
do_move (GVfsBackend *backend,
         GVfsJobMove *job,
         const char *source,
         const char *destination,
         GFileCopyFlags flags,
         GFileProgressCallback progress_callback,
         gpointer progress_callback_data)
{
  GVfsBackendArchive *ba = G_VFS_BACKEND_ARCHIVE (backend);
  GVfsArchive *archive;
  ArchiveFile *destination_file;
  ArchiveFile *source_file;
  struct archive_entry *entry;
  const char *pathname;
  char *name;
  
  DEBUG ("move %s to %s\n", source, destination);
  
  /* Lock backend for write. */
  if (!g_mutex_trylock (ba->write_lock))
    {
      g_vfs_job_failed (G_VFS_JOB (job), 
                        G_IO_ERROR,
                        G_IO_ERROR_BUSY,
                        _("Can't do multiple write operations"));
      return;
    }
  
  /* Check validity of the file name. */
  if (strcmp (source, "/") == 0)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));
      g_mutex_unlock (ba->write_lock);
      
      return;
    }
  
  /* Check whether the source file exists. */
  source_file = archive_file_find (ba, source);
  if (source_file == NULL)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_FOUND,
                        _("File doesn't exist"));
      g_mutex_unlock (ba->write_lock);
      
      return;
    }
  
  /* Pathnames are equal. */
  if (strcmp (source, destination) == 0)
    {
      g_mutex_unlock (ba->write_lock);
      g_vfs_job_succeeded (G_VFS_JOB (job));
      
      return;
    }
  
  /* Check whether the destination file does not exists. */
  destination_file = archive_file_find (ba, destination);
  if (destination_file != NULL)
    {
      if (flags & G_FILE_COPY_OVERWRITE)
        {
          if ((g_file_info_get_file_type (source_file->info) 
                 == G_FILE_TYPE_DIRECTORY) && 
              (g_file_info_get_file_type (destination_file->info) 
                 == G_FILE_TYPE_DIRECTORY))
            {
                  g_vfs_job_failed (G_VFS_JOB (job), 
                                    G_IO_ERROR,
                                    G_IO_ERROR_WOULD_MERGE,
                                    _("Can't move directory over directory"));
                  g_mutex_unlock (ba->write_lock);
                  
                  return;
            }
        }
      else
        {
          g_vfs_job_failed (G_VFS_JOB (job), 
                            G_IO_ERROR,
                            G_IO_ERROR_EXISTS,
                            _("Target file already exists"));
          g_mutex_unlock (ba->write_lock);
          
          return;
        }  
    }

  if (!(flags & G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS))
    g_warning ("FIXME: follow symlinks");
  
  /* Move the file in the archive. */
  archive = gvfs_archive_readwrite_new (ba, G_VFS_JOB (job));
  entry = gvfs_archive_copy_prefix (archive, destination + 1, source + 1);
  while (entry != NULL)
    {
      pathname = archive_entry_pathname (entry);
      if (g_str_has_prefix (pathname, source + 1))
        {
          /* Change the name in the archive entry. */
          name = g_build_filename (destination + 1, 
                                   pathname + strlen (source) - 1, 
                                   NULL);
          archive_entry_set_pathname (entry, name);
          g_free (name);
          
          /* Write the header and data. */
          gvfs_archive_write_header (archive, entry);
          gvfs_archive_copy_data (archive);
        }
      else
        archive_read_data_skip (archive->archive);
      
      entry = gvfs_archive_copy_prefix (archive, destination + 1, source + 1);
    }
  
  if (!gvfs_archive_in_error (archive))
    {
      /* Move the file in the file tree. */
      g_mutex_lock (ba->read_lock);
      
      if (destination_file == NULL)
        destination_file = archive_file_get_from_path (ba->files, 
                                                       destination + 1, 
                                                       TRUE);
      source_file->parent->children = 
        g_slist_remove (source_file->parent->children, source_file);
      destination_file->parent->children = 
        g_slist_remove (destination_file->parent->children, destination_file);
      destination_file->parent->children = 
        g_slist_append (destination_file->parent->children, source_file);
      source_file->parent = destination_file->parent;
      archive_file_free (destination_file);
      
      /* Set correct info. */
      g_free (source_file->name);
      source_file->name = g_path_get_basename (destination);
      g_file_info_set_name (source_file->info, source_file->name);
      gvfs_file_info_populate_default (source_file->info,
        source_file->name,
        g_file_info_get_file_type (source_file->info));
      
      g_mutex_unlock (ba->read_lock);
    }
  
  g_mutex_unlock (ba->write_lock);
  gvfs_archive_finish (archive);
}

/**
 * Delete file inside the archive.
 * @note The GVFS operation.
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param pathname File pathname to delete.
 */
static void
do_delete (GVfsBackend   *backend,
           GVfsJobDelete *job,
           const char    *pathname)
{
  GVfsBackendArchive *ba = G_VFS_BACKEND_ARCHIVE (backend);
  GVfsArchive *archive;
  struct archive_entry *entry;
  ArchiveFile *file;
  
  DEBUG ("delete %s\n", pathname);
  
  /* Lock backend for write. */
  if (!g_mutex_trylock (ba->write_lock))
    {
      g_vfs_job_failed (G_VFS_JOB (job), 
                        G_IO_ERROR,
                        G_IO_ERROR_BUSY,
                        _("Can't do multiple write operations"));
      return;
    }
  
  /* Check validity of the file name. */
  if (strcmp (pathname, "/") == 0)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));
      g_mutex_unlock (ba->write_lock);
      
      return;
    }
  
  /* Check whether the source file exists. */
  file = archive_file_find (ba, pathname);
  if (file == NULL)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_FOUND,
                        _("File doesn't exist"));
      g_mutex_unlock (ba->write_lock);
      
      return;
    }
  
  /* Delete the file from the archive file. */
  archive = gvfs_archive_readwrite_new (ba, G_VFS_JOB (job));
  entry = gvfs_archive_copy_prefix (archive, pathname + 1, NULL);
  while (entry != NULL)
    {
      archive_read_data_skip (archive->archive);
      
      entry = gvfs_archive_copy_prefix (archive, pathname + 1, NULL);
    }
  
  if (!gvfs_archive_in_error (archive))
    {
      /* Delete the file from the file tree. */
      g_mutex_lock (ba->read_lock);
      
      file->parent->children = g_slist_remove (file->parent->children, file);
      archive_file_free (file);
      
      g_mutex_unlock (ba->read_lock);
    }
  
  g_mutex_unlock (ba->write_lock);
  gvfs_archive_finish (archive);  
}

/**
 * Make a new directory inside the archive.
 * @note The GVFS operation.
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param pathname Directory pathname to create.
 */
static void
do_make_directory (GVfsBackend          *backend,
                   GVfsJobMakeDirectory *job,
                   const char           *pathname)
{
  GVfsBackendArchive *ba = G_VFS_BACKEND_ARCHIVE (backend);
  GVfsArchive *archive;
  struct archive_entry *entry;
  ArchiveFile *file;
    
  DEBUG ("make a directory %s\n", pathname);
  
  /* Lock backend for write. */
  if (!g_mutex_trylock (ba->write_lock))
    {
      g_vfs_job_failed (G_VFS_JOB (job), 
                        G_IO_ERROR,
                        G_IO_ERROR_BUSY,
                        _("Can't do multiple write operations"));
      return;
    }
  
  /* Check whether the destination file does not exists. */
  if (archive_file_find (ba, pathname) != NULL)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
                        G_IO_ERROR,
                        G_IO_ERROR_EXISTS,
                        _("File already exists"));
      g_mutex_unlock (ba->write_lock);
      
      return; 
    }
  
  /* Copy the whole archive. */
  archive = gvfs_archive_readwrite_new (ba, G_VFS_JOB (job));
  gvfs_archive_copy (archive);
  
  /* Add the directory into the archive. */
  entry = archive_entry_new ();
  archive_entry_set_filetype (entry, AE_IFDIR);
  archive_entry_set_pathname (entry, pathname + 1);
  archive_entry_set_perm (entry, 0755);
  gvfs_archive_write_header (archive, entry);
  
  if (!gvfs_archive_in_error (archive))
    {
      /* Add the file into the file tree. */
      g_mutex_lock (ba->read_lock);
      
      file = archive_file_get_from_path (ba->files, 
                                         pathname + 1, 
                                         TRUE);
      fixup_dirs (ba->files);
      
      g_mutex_unlock (ba->read_lock);
    }
  
  g_mutex_unlock (ba->write_lock);
  archive_entry_free (entry);
  gvfs_archive_finish (archive);  
}

/**
 * Get info about the file inside the archive.
 * @note The GVFS operation.
 *       For details about flags and structures see GFile documentation page. 
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param filename File pathname to get info.
 * @param flags Query flags.
 * @param info Info structure (set by function).
 * @param attribute_matcher Filter for matching attributes.
 * @see try_query_fs_info
 */
static void
do_query_info (GVfsBackend *backend,
	       GVfsJobQueryInfo *job,
	       const char *filename,
	       GFileQueryInfoFlags flags,
	       GFileInfo *info,
	       GFileAttributeMatcher *attribute_matcher)
{
  GVfsBackendArchive *ba = G_VFS_BACKEND_ARCHIVE (backend);
  ArchiveFile *file;

  g_mutex_lock (ba->read_lock);
  
  file = archive_file_find (ba, filename);
  if (file == NULL)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
		        G_IO_ERROR,
			G_IO_ERROR_NOT_FOUND,
			_("File doesn't exist"));
      g_mutex_unlock (ba->read_lock);
      
      return;
    }

  if (!(flags & G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS))
    g_warning ("FIXME: follow symlinks");

  g_file_info_copy_into (file->info, info);
  
  g_mutex_unlock (ba->read_lock);
  
  g_vfs_job_succeeded (G_VFS_JOB (job));
}

/**
 * Enumerate children of dir inside the archive.
 * @note The GVFS operation.
 *       For details about flags and structures see GFile documentation page. 
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param filename Dir pathname to get children.
 * @param attribute_matcher Filter for matching attributes.
 * @param flags Query flags.
 */
static void
do_enumerate (GVfsBackend *backend,
	      GVfsJobEnumerate *job,
	      const char *filename,
	      GFileAttributeMatcher *attribute_matcher,
	      GFileQueryInfoFlags flags)
{
  GVfsBackendArchive *ba = G_VFS_BACKEND_ARCHIVE (backend);
  ArchiveFile *file;
  GSList *walk;

  g_mutex_lock (ba->read_lock);
  
  file = archive_file_find (ba, filename);
  if (file == NULL)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
		        G_IO_ERROR,
			G_IO_ERROR_NOT_FOUND,
			_("File doesn't exist"));
      g_mutex_unlock (ba->read_lock);
      
      return;
    }

  if (g_file_info_get_file_type (file->info) != G_FILE_TYPE_DIRECTORY)
    {
      g_vfs_job_failed (G_VFS_JOB (job),
		        G_IO_ERROR,
			G_IO_ERROR_NOT_DIRECTORY,
			_("The file is not a directory"));
      g_mutex_unlock (ba->read_lock);
      
      return;
    }

  if (!(flags & G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS))
    g_warning ("FIXME: follow symlinks");

  for (walk = file->children; walk; walk = walk->next)
    {
      GFileInfo *info = g_file_info_dup (((ArchiveFile *) walk->data)->info);
      g_vfs_job_enumerate_add_info (job, info);
      g_object_unref (info);
    }
  g_vfs_job_enumerate_done (job);
  
  g_mutex_unlock (ba->read_lock);
  
  g_vfs_job_succeeded (G_VFS_JOB (job));
}

/**
 * Get info about the archive filesystem.
 * @note The GVFS operation.
 *       For details about flags and structures see GFile documentation page. 
 * @param backend ::GVfsBackendArchive structure.
 * @param job GVFS job.
 * @param filename File pathname to get info.
 * @param info Info structure (set by function).
 * @param attribute_matcher Filter for matching attributes.
 * @see do_query_info
 */
static gboolean
try_query_fs_info (GVfsBackend *backend,
                   GVfsJobQueryFsInfo *job,
                   const char *filename,
                   GFileInfo *info,
                   GFileAttributeMatcher *attribute_matcher)
{
  g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_FILESYSTEM_READONLY, FALSE); 
  g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_FILESYSTEM_USE_PREVIEW, G_FILESYSTEM_PREVIEW_TYPE_IF_LOCAL);
  g_vfs_job_succeeded (G_VFS_JOB (job));
  return TRUE;
}

static void
g_vfs_backend_archive_class_init (GVfsBackendArchiveClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GVfsBackendClass *backend_class = G_VFS_BACKEND_CLASS (klass);
  
  gobject_class->finalize = g_vfs_backend_archive_finalize;

  backend_class->mount = do_mount;
  backend_class->unmount = do_unmount;
  backend_class->open_for_read = do_open_for_read;
  backend_class->close_read = do_close_read;
  backend_class->read = do_read;
  backend_class->enumerate = do_enumerate;
  backend_class->query_info = do_query_info;
  backend_class->try_query_fs_info = try_query_fs_info;
  backend_class->push = do_push;
  backend_class->set_display_name = do_set_display_name;
  backend_class->delete = do_delete;
  backend_class->make_directory = do_make_directory;
  backend_class->move = do_move;
}
