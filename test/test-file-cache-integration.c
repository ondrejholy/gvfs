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
 * It tests if reading and seeking works properly.
 *
 * ./test-file-cache-integration localtest:///home/ondra
 */

GFile *remote_test_dir_file;

#define TEST_CONTENT "abcdefghijklmnopqrstvwxyz"
#define TEST_LENGTH 26

/* Create test file inside test dir */
static GFile *
create_test_file (GFile *dir, const gchar *name, const gchar *content, gssize size)
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
  g_output_stream_write (G_OUTPUT_STREAM (stream), content, size, NULL, &e);
  g_assert_no_error (e);
  g_output_stream_close (G_OUTPUT_STREAM (stream), NULL, &e);
  g_assert_no_error (e);

  return file;
}

/* Delete test file */
static void
delete_test_file (GFile *file)
{
  GError *e = NULL;

  g_file_delete (file, NULL, &e);
  g_assert_no_error (e);
}

static void
test_read ()
{
  GError *e = NULL;
  GFileInputStream *s;
  GFile *f;
  gssize r;
  gchar b[TEST_LENGTH];
  
  f = create_test_file (remote_test_dir_file, "A", TEST_CONTENT, TEST_LENGTH);

  s = g_file_read (f, NULL, &e);
  g_assert_no_error (e);
  g_assert (s != NULL);
  g_input_stream_read_all (G_INPUT_STREAM (s), b, TEST_LENGTH, &r, NULL, &e);
  g_assert_no_error (e);
  g_assert_cmpstr (b, ==, TEST_CONTENT);

  delete_test_file (f);
}

static void
test_seek ()
{
  GError *e = NULL;
  GFileInputStream *s;
  GFile *f;
  gssize r;
  gchar b[TEST_LENGTH];
  
  f = create_test_file (remote_test_dir_file, "A", TEST_CONTENT, TEST_LENGTH);

  s = g_file_read (f, NULL, &e);
  g_assert_no_error (e);
  g_assert (s != NULL);
  g_input_stream_read_all (G_INPUT_STREAM (s), b, TEST_LENGTH, &r, NULL, &e);
  g_assert_no_error (e);
  g_assert_cmpstr (b, ==, TEST_CONTENT);
  
  g_assert (g_seekable_can_seek (G_SEEKABLE (s)));
  g_assert_cmpint (g_seekable_tell (G_SEEKABLE (s)), !=, 0);
  g_seekable_seek (G_SEEKABLE (s), 0, G_SEEK_SET, NULL, &e);
  g_assert_cmpint (g_seekable_tell (G_SEEKABLE (s)), ==, 0);
  g_assert_no_error (e);

  g_input_stream_read_all (G_INPUT_STREAM (s), b, TEST_LENGTH, &r, NULL, &e);
  g_assert_no_error (e);
  g_assert_cmpstr (b, ==, TEST_CONTENT);

  delete_test_file (f);
}

int
main (int argc, char *argv[])
{
  gint res;

  g_test_init (&argc, &argv, NULL);
  if (argc != 2)
  {
    g_printerr ("usage: test-file-cache-integration <remote_test_dir_uri>\n");
    return 1;
  }

  remote_test_dir_file = g_file_new_for_commandline_arg (argv[1]);
  res = g_file_query_exists (remote_test_dir_file, NULL);
  if (!res)
  {
    g_object_unref (remote_test_dir_file);
    g_printerr ("remote dir has to be mounted and writable\n");
    return 1;
  }

  g_test_add_func ("/file_cache/read", test_read);
  g_test_add_func ("/file_cache/seek", test_seek);

  res = g_test_run ();

  g_object_unref (remote_test_dir_file);

  return res;
}
