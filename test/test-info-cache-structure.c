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

#include "gio/gio.h"
#include "gvfsinfocache.h"

/*
 * It tests operations of info cache structure.
 */

static void
test_new_free ()
{
  GVfsInfoCache *c;

  c = g_vfs_info_cache_new (0, 0);
  g_assert (c != NULL);
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 0);

  g_vfs_info_cache_free (c);
}

static void
test_insert_remove ()
{
  GVfsInfoCache *c;

  c = g_vfs_info_cache_new (0, 0);

  g_vfs_info_cache_insert (c, g_strdup ("A"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 1);

  g_vfs_info_cache_insert (c, g_strdup ("B"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 2);

  /* update */
  g_vfs_info_cache_insert (c, g_strdup ("B"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 2);

  g_vfs_info_cache_remove (c, "A");
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 1);

  /* missing */
  g_vfs_info_cache_remove (c, "C");
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 1);

  g_vfs_info_cache_insert (c, g_strdup ("A"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 2);

  g_vfs_info_cache_remove_all (c);
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 0);

  g_vfs_info_cache_free (c);
}

static void
test_find ()
{
  GVfsInfoCache *c;
  GFileInfo *i, *r;
  GFileAttributeMatcher *m;

  c = g_vfs_info_cache_new (0, 0);
  m = g_file_attribute_matcher_new ("*");

  i = g_file_info_new ();
  g_vfs_info_cache_insert (c, g_strdup ("A"), i, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r == i);
  g_object_unref (r);

  i = g_file_info_new ();
  g_vfs_info_cache_insert (c, g_strdup ("B"), i, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r != NULL);
  g_object_unref (r);

  r = g_vfs_info_cache_find (c, "B", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r == i);
  g_object_unref (r);

  /* update */
  i = g_file_info_new ();
  g_vfs_info_cache_insert (c, g_strdup ("B"), i, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  r = g_vfs_info_cache_find (c, "B", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r == i);
  g_object_unref (r);

  /* missing */
  r = g_vfs_info_cache_find (c, "C", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r == NULL);

  g_file_attribute_matcher_unref (m);
  g_vfs_info_cache_free (c);
}

static void
test_attributes ()
{
  GVfsInfoCache *c;
  GFileInfo *r;
  GFileAttributeMatcher *m;

  c = g_vfs_info_cache_new (0, 0);

  g_vfs_info_cache_insert (c, g_strdup ("A"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  m = g_file_attribute_matcher_new ("standard::*");
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_file_attribute_matcher_unref (m);
  g_assert (r != NULL);
  g_object_unref (r);

  g_vfs_info_cache_insert (c, g_strdup ("A"), g_file_info_new (), g_file_attribute_matcher_new ("standard::*"), G_FILE_QUERY_INFO_NONE);
  m = g_file_attribute_matcher_new ("standard::name");
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_file_attribute_matcher_unref (m);
  g_assert (r != NULL);
  g_object_unref (r);

  g_vfs_info_cache_insert (c, g_strdup ("A"), g_file_info_new (), g_file_attribute_matcher_new ("standard::*"), G_FILE_QUERY_INFO_NONE);
  m = g_file_attribute_matcher_new ("*");
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_file_attribute_matcher_unref (m);
  g_assert (r == NULL);

  g_vfs_info_cache_insert (c, g_strdup ("A"), g_file_info_new (), g_file_attribute_matcher_new ("standard::*"), G_FILE_QUERY_INFO_NONE);
  m = g_file_attribute_matcher_new ("unix::*");
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_file_attribute_matcher_unref (m);
  g_assert (r == NULL);

  g_vfs_info_cache_free (c);
}

static void
test_flags ()
{
  GVfsInfoCache *c;
  GFileInfo *i, *r;
  GFileAttributeMatcher *m;

  c = g_vfs_info_cache_new (0, 0);
  m = g_file_attribute_matcher_new ("*");

  g_vfs_info_cache_insert (c, g_strdup ("A"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r != NULL);
  g_object_unref (r);

  g_vfs_info_cache_insert (c, g_strdup ("A"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
  g_assert (r != NULL);
  g_object_unref (r);

  i = g_file_info_new ();
  g_file_info_set_is_symlink (i, FALSE);
  g_vfs_info_cache_insert (c, g_strdup ("A"), i, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r != NULL);
  g_object_unref (r);

  i = g_file_info_new ();
  g_file_info_set_is_symlink (i, FALSE);
  g_vfs_info_cache_insert (c, g_strdup ("A"), i, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
  g_assert (r == NULL);

  i = g_file_info_new ();
  g_file_info_set_is_symlink (i, TRUE);
  g_vfs_info_cache_insert (c, g_strdup ("A"), i, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r == NULL);

  i = g_file_info_new ();
  g_file_info_set_is_symlink (i, TRUE);
  g_vfs_info_cache_insert (c, g_strdup ("A"), i, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
  g_assert (r == NULL);

  g_file_attribute_matcher_unref (m);
  g_vfs_info_cache_free (c);
}

static void
test_max_time ()
{
  GVfsInfoCache *c;
  GFileInfo *r;
  GFileAttributeMatcher *m;

  c = g_vfs_info_cache_new (0, 1);
  m = g_file_attribute_matcher_new ("*");

  g_vfs_info_cache_insert (c, g_strdup ("A"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r != NULL);
  g_object_unref (r);

  /* timeout */
  g_usleep (1.1 * G_USEC_PER_SEC);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r == NULL);

  /* garbage collector */
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 0);

  g_file_attribute_matcher_unref (m);
  g_vfs_info_cache_free (c);
}

static void
test_max_count ()
{
  GVfsInfoCache *c;
  GFileInfo *r;
  GFileAttributeMatcher *m;

  c = g_vfs_info_cache_new (2, 0);
  m = g_file_attribute_matcher_new ("*");

  /* B, A */
  g_vfs_info_cache_insert (c, g_strdup ("A"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  g_vfs_info_cache_insert (c, g_strdup ("B"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 2);

  /* C, B */
  g_vfs_info_cache_insert (c, g_strdup ("C"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r == NULL);
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 2);

  /* B, C */
  r = g_vfs_info_cache_find (c, "B", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r != NULL);
  g_object_unref (r);
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 2);

  /* A, B */
  g_vfs_info_cache_insert (c, g_strdup ("A"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  r = g_vfs_info_cache_find (c, "C", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r == NULL);
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 2);

  /* B, A (update) */
  g_vfs_info_cache_insert (c, g_strdup ("B"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 2);

  /* C, B */
  g_vfs_info_cache_insert (c, g_strdup ("C"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r == NULL);
  g_assert_cmpuint (g_vfs_info_cache_get_count (c), ==, 2);

  g_file_attribute_matcher_unref (m);
  g_vfs_info_cache_free (c);
}

static void
test_enable_disable ()
{
  GVfsInfoCache *c;
  GFileInfo *r;
  GFileAttributeMatcher *m;

  c = g_vfs_info_cache_new (0, 0);
  m = g_file_attribute_matcher_new ("*");
 
  g_vfs_info_cache_disable (c);
  g_assert (g_vfs_info_cache_is_disabled (c));
  g_vfs_info_cache_insert (c, g_strdup ("B"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  g_vfs_info_cache_enable (c);
  g_assert (!g_vfs_info_cache_is_disabled (c));
  r = g_vfs_info_cache_find (c, "B", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r == NULL);

  g_vfs_info_cache_insert (c, g_strdup ("A"), g_file_info_new (), g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE);
  g_vfs_info_cache_disable (c);
  r = g_vfs_info_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE);
  g_assert (r != NULL);
  g_object_unref (r);
  g_vfs_info_cache_enable (c);

  /* multiple */ 
  g_vfs_info_cache_disable (c);
  g_vfs_info_cache_disable (c);
  g_assert (g_vfs_info_cache_is_disabled (c));
  g_vfs_info_cache_enable (c);
  g_assert (g_vfs_info_cache_is_disabled (c));
  g_vfs_info_cache_enable (c);
  g_assert (!g_vfs_info_cache_is_disabled (c));

  g_file_attribute_matcher_unref (m);
  g_vfs_info_cache_free (c);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/info_cache/new_free", test_new_free);
  g_test_add_func ("/info_cache/insert_remove", test_insert_remove);
  g_test_add_func ("/info_cache/find", test_find);
  g_test_add_func ("/info_cache/attributes", test_attributes);
  g_test_add_func ("/info_cache/flags", test_flags);
  g_test_add_func ("/info_cache/max_time", test_max_time);
  g_test_add_func ("/info_cache/max_count", test_max_count);
  g_test_add_func ("/info_cache/enable_disable", test_enable_disable);
  
  return g_test_run ();
}
