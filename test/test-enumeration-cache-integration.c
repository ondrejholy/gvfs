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

#include <string.h>
#include "gio/gio.h"

/*
 * It tests if cache is invalidated properly after writing operations.
 * 
 * N.B.: Some tests may not work or may test something else, it depends
 * on implemented methods of the concrete backend.
 *
 * ./test-enumeration-cache-integration /home/ondra/ localtest:///home/ondra
 */

char *local_test_dir_path;
char *remote_test_dir_uri;
GFile *local_test_dir_file;
GFile *remote_test_dir_file;

#define TEST_TIME 42

/* Create test file inside test dir */
static GFile *
create_test_file (GFile *dir, const gchar *name)
{
  GError *e = NULL;
  GFile *file;
  GFileOutputStream *stream;

  file = g_file_get_child (dir, name);
  stream = g_file_create (file, G_FILE_CREATE_REPLACE_DESTINATION, NULL, &e);
  if (g_error_matches (e, G_IO_ERROR, G_IO_ERROR_EXISTS))
  { /* FIXME: Why G_FILE_CREATE_REPLACE_DESTINATION doesn't work? */
    g_clear_error (&e);
    g_file_delete (file, NULL, &e);
    stream = g_file_create (file, G_FILE_CREATE_REPLACE_DESTINATION, NULL, &e);
  }
  g_assert_no_error (e);
  g_output_stream_write (G_OUTPUT_STREAM (stream), name, strlen (name), NULL, &e);
  g_assert_no_error (e);
  g_output_stream_close (G_OUTPUT_STREAM (stream), NULL, &e);
  g_assert_no_error (e);

  return file;
}

/* Delete test dir with its content */
static void
delete_test_dir (GFile *dir)
{
  GFile *file;
  GFileInfo *info;
  GFileEnumerator *enumerator;
  const char *name;
  GError *e = NULL;
  
  enumerator = g_file_enumerate_children (dir,
                                          G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          NULL,
                                          NULL);
  info = g_file_enumerator_next_file (enumerator, NULL, NULL);
  while (info != NULL)
  {
    name = g_file_info_get_display_name (info);
    file = g_file_get_child_for_display_name (dir, name, NULL);
    g_file_delete (file, NULL, &e);
    g_assert_no_error (e);
 
    info = g_file_enumerator_next_file (enumerator, NULL, NULL);
  }

  g_file_delete (dir, NULL, NULL);
}

/* Create test dir */
static void
create_test_dir (GFile *dir)
{
  GError *e = NULL;

  g_file_make_directory (dir, NULL, &e);
  if (g_error_matches (e, G_IO_ERROR, G_IO_ERROR_EXISTS))
  {
    delete_test_dir (dir);
    g_clear_error (&e);
    g_file_make_directory (dir, NULL, &e);
  }

  g_assert_no_error (e);
}

static void
test_setattribute ()
{
  GFile *f, *d = remote_test_dir_file;
  GFileInfo *i;
  GError *e = NULL;
  GTimeVal r;
  GFileEnumerator *er;

  create_test_dir (d);

  f = create_test_file (d, "A");
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_object_unref (i);

  g_file_set_attribute_uint64 (f, G_FILE_ATTRIBUTE_TIME_MODIFIED, TEST_TIME, G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_file_info_get_modification_time (i, &r);
  g_assert_cmpint (r.tv_sec, ==, TEST_TIME);
  g_object_unref (i);
  g_object_unref (f);

  delete_test_dir (remote_test_dir_file);
}

static void
test_delete ()
{
  GFile *f, *d = remote_test_dir_file;
  GFileInfo *i;
  GError *e = NULL;
  GFileEnumerator *er;

  create_test_dir (d);

  f = create_test_file (remote_test_dir_file, "A");
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_object_unref (i);

  g_file_delete (f, NULL, &e);
  g_assert_no_error (e);
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert (i == NULL);
  g_object_unref (f);

  delete_test_dir (d);
}

static void
test_trash ()
{
  GFile *f, *d = remote_test_dir_file;
  GFileInfo *i;
  GError *e = NULL;
  GFileEnumerator *er;

  create_test_dir (remote_test_dir_file);

  f = create_test_file (remote_test_dir_file, "A");
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_object_unref (i);

  g_file_trash (f, NULL, &e);
  g_assert_no_error (e);
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert (i == NULL);
  g_object_unref (f);

  delete_test_dir (remote_test_dir_file);
}

static void
test_pull ()
{
  GFile *f, *f2, *d = remote_test_dir_file;
  GFileInfo *i;
  GError *e = NULL;
  GFileEnumerator *er;

  create_test_dir (local_test_dir_file);
  create_test_dir (remote_test_dir_file);

  f = create_test_file (remote_test_dir_file, "A");
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_object_unref (i);

  f2 = g_file_get_child (local_test_dir_file, "B");
  g_file_move (f, f2, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &e);
  g_assert_no_error (e);
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_assert (i == NULL);
  g_object_unref (f);
  g_object_unref (f2);

  delete_test_dir (local_test_dir_file);
  delete_test_dir (remote_test_dir_file);
}

static void
test_push ()
{
  GFile *f, *f2, *d = remote_test_dir_file;
  GFileInfo *i;
  GError *e = NULL;
  GTimeVal r;
  GFileEnumerator *er;

  create_test_dir (local_test_dir_file);
  create_test_dir (remote_test_dir_file);

  f = create_test_file (remote_test_dir_file, "A");
  g_file_set_attribute_uint64 (f, G_FILE_ATTRIBUTE_TIME_MODIFIED, TEST_TIME, G_FILE_QUERY_INFO_NONE, NULL, &e);
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_object_unref (i);

  f2 = create_test_file (local_test_dir_file, "B");
  g_file_move (f2, f, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &e);
  g_assert_no_error (e);
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_file_info_get_modification_time (i, &r);
  g_assert_cmpint (r.tv_sec, !=, TEST_TIME);
  g_object_unref (i);
  g_object_unref (f);
  g_object_unref (f2);

  delete_test_dir (local_test_dir_file);
  delete_test_dir (remote_test_dir_file);
}

static void
test_setdisplayname ()
{
  GFile *f, *f2, *d = remote_test_dir_file;
  GFileInfo *i;
  GError *e = NULL;
  GFileEnumerator *er;

  create_test_dir (remote_test_dir_file);

  f = create_test_file (remote_test_dir_file, "A");
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_object_unref (i);

  g_file_set_display_name (f, "B", NULL, &e);
  g_assert_no_error (e);
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_assert_cmpstr (g_file_info_get_name (i), ==, "B");
  g_object_unref (f);

  delete_test_dir (remote_test_dir_file);
}

static void
test_move ()
{
  GFile *f, *f2, *d = remote_test_dir_file;
  GFileInfo *i;
  GError *e = NULL;
  GTimeVal r;
  GFileEnumerator *er;

  create_test_dir (remote_test_dir_file);

  f = create_test_file (remote_test_dir_file, "A");
  f2 = create_test_file (remote_test_dir_file, "B");
  g_file_set_attribute_uint64 (f2, G_FILE_ATTRIBUTE_TIME_MODIFIED, TEST_TIME, G_FILE_QUERY_INFO_NONE, NULL, &e);
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_object_unref (i);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_object_unref (i);

  g_file_move (f, f2, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &e);
  g_assert_no_error (e);
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_file_info_get_modification_time (i, &r);
  g_assert_cmpint (r.tv_sec, !=, TEST_TIME);
  g_object_unref (i);
  g_object_unref (f);
  g_object_unref (f2);

  delete_test_dir (remote_test_dir_file);
}

static void
test_copy ()
{
  GFile *f, *f2, *d = remote_test_dir_file;
  GFileInfo *i;
  GError *e = NULL;
  GTimeVal r;
  GFileEnumerator *er;

  create_test_dir (remote_test_dir_file);

  f = create_test_file (remote_test_dir_file, "A");
  f2 = create_test_file (remote_test_dir_file, "B");
  g_file_set_attribute_uint64 (f2, G_FILE_ATTRIBUTE_TIME_MODIFIED, TEST_TIME, G_FILE_QUERY_INFO_NONE, NULL, &e);
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_object_unref (i);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_object_unref (i);

  g_file_copy (f, f2, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &e);
  g_assert_no_error (e);
  er = g_file_enumerate_children (d, "*", G_FILE_QUERY_INFO_NONE, NULL, &e);
  g_assert_no_error (e);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_file_info_get_modification_time (i, &r);
  g_assert_cmpint (r.tv_sec, !=, TEST_TIME);
  g_object_unref (i);
  i = g_file_enumerator_next_file (er, NULL, &e);
  g_assert_no_error (e);
  g_file_info_get_modification_time (i, &r);
  g_assert_cmpint (r.tv_sec, !=, TEST_TIME);
  g_object_unref (i);
  g_object_unref (f);
  g_object_unref (f2);

  delete_test_dir (remote_test_dir_file);
}

int
main (int argc, char *argv[])
{
  gint res;
  GFileInfo *info;
  GFile *remote;

  g_test_init (&argc, &argv, NULL);
  if (argc != 3)
  {
    g_printerr ("usage: test-enumeration-cache-integration <local_test_dir_path> <remote_test_dir_uri>\n");
    return 1;
  }

  remote = g_file_new_for_uri (argv[2]);
  res = g_file_query_exists (remote, NULL);
  g_object_unref (remote);
  if (!res)
  {
    g_printerr ("remote dir has to be mounted and writable\n");
    return 1;
  }

  local_test_dir_path = g_build_filename (argv[1], "test-enumeration-cache-local", NULL);
  remote_test_dir_uri = g_build_filename (argv[2], "test-enumeration-cache-remote", NULL);
  local_test_dir_file = g_file_new_for_commandline_arg (local_test_dir_path);
  remote_test_dir_file = g_file_new_for_commandline_arg (remote_test_dir_uri);

  g_test_add_func ("/enumerate_cache/setattribute", test_setattribute);
  g_test_add_func ("/enumerate_cache/delete", test_delete);
  g_test_add_func ("/enumerate_cache/trash", test_trash);
  g_test_add_func ("/enumerate_cache/pull", test_pull);
  g_test_add_func ("/enumerate_cache/push", test_push);
  g_test_add_func ("/enumerate_cache/setdisplayname", test_setdisplayname);
  g_test_add_func ("/enumerate_cache/move", test_move);
  g_test_add_func ("/enumerate_cache/copy", test_copy);

  res = g_test_run ();

  g_free (local_test_dir_path);
  g_free (remote_test_dir_uri);
  g_object_unref (local_test_dir_file);
  g_object_unref (remote_test_dir_file);

  return res;
}
