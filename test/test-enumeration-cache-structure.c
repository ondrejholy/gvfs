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
#include "gvfsenumerationcache.h"

/*
 * It tests operations of enumeration cache structure.
 */

static void
test_new_free ()
{
  GVfsEnumerationCache *c;

  c = g_vfs_enumeration_cache_new (0, 0);
  g_assert (c != NULL);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 0);

  g_vfs_enumeration_cache_free (c);
}

static void
test_insert_remove ()
{
  GVfsEnumerationCache *c;
  GList *l;
  gint64 s;

  c = g_vfs_enumeration_cache_new (0, 0);

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 1);

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("B"));
  g_vfs_enumeration_cache_set (c, g_strdup ("B"), l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 2);

  /* update */
  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("B"));
  g_vfs_enumeration_cache_set (c, g_strdup ("B"), l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 2);

  g_vfs_enumeration_cache_remove (c, "A");
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 1);

  /* empty */
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("C"));
  g_vfs_enumeration_cache_set (c, g_strdup ("C"), NULL, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 0);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 1);

  /* missing */
  g_vfs_enumeration_cache_remove (c, "D");
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 1);

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 2);

  g_vfs_enumeration_cache_remove_all (c);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 0);

  g_vfs_enumeration_cache_free (c);
}

static void
test_find ()
{
  GVfsEnumerationCache *c;
  GList *l, *r;
  GFileAttributeMatcher *m;
  guint64 s;
  guint count;

  c = g_vfs_enumeration_cache_new (0, 0);
  m = g_file_attribute_matcher_new ("*");

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r->data == l->data);
  g_assert_cmpuint (count, ==, 1);
  g_list_free_full (r, g_object_unref);

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("B"));
  g_vfs_enumeration_cache_set (c, "B", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r != NULL);
  g_assert_cmpuint (count, ==, 1);
  g_list_free_full (r, g_object_unref);

  r = g_vfs_enumeration_cache_find (c, "B", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r->data == l->data);
  g_assert_cmpuint (count, ==, 1);
  g_list_free_full (r, g_object_unref);

  /* update */
  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("B"));
  g_vfs_enumeration_cache_set (c, "B", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  r = g_vfs_enumeration_cache_find (c, "B", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r->data == l->data);
  g_assert_cmpuint (count, ==, 1);
  g_list_free_full (r, g_object_unref);

  /* empty */
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("C"));
  g_vfs_enumeration_cache_set (c, "C", NULL, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 0);
  r = g_vfs_enumeration_cache_find (c, "C", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r == NULL);
  g_assert_cmpuint (count, ==, 0);

  /* missing */
  r = g_vfs_enumeration_cache_find (c, "D", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r == NULL);
  g_assert_cmpuint (count, ==, G_MAXUINT);

  g_file_attribute_matcher_unref (m);
  g_vfs_enumeration_cache_free (c);
}

static void
test_attributes ()
{
  GVfsEnumerationCache *c;
  GList *l, *r;
  GFileAttributeMatcher *m;
  gint64 s;
  guint count;

  c = g_vfs_enumeration_cache_new (0, 0);

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  m = g_file_attribute_matcher_new ("standard::*");
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_file_attribute_matcher_unref (m);  
  g_assert (r != NULL);
  g_list_free_full (r, g_object_unref);

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("standard::*"), G_FILE_QUERY_INFO_NONE, s, 1);
  m = g_file_attribute_matcher_new ("standard::name");
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_file_attribute_matcher_unref (m);  
  g_assert (r != NULL);
  g_list_free_full (r, g_object_unref);

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("standard::*"), G_FILE_QUERY_INFO_NONE, s, 1);
  m = g_file_attribute_matcher_new ("*");
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_file_attribute_matcher_unref (m);  
  g_assert (r == NULL);

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("standard::*"), G_FILE_QUERY_INFO_NONE, s, 1);
  m = g_file_attribute_matcher_new ("unix::*");
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_file_attribute_matcher_unref (m);  
  g_assert (r == NULL);

  g_vfs_enumeration_cache_free (c);
}

static void
test_flags ()
{
  GVfsEnumerationCache *c;
  GList *l, *r;
  GFileAttributeMatcher *m;
  gint64 s;
  guint count;

  c = g_vfs_enumeration_cache_new (0, 0);
  m = g_file_attribute_matcher_new ("*");

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r != NULL);
  g_list_free_full (r, g_object_unref);

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, s, 1);
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, &count);
  g_assert (r != NULL);
  g_list_free_full (r, g_object_unref);

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, s, 1);
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r == NULL);

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, &count);
  g_assert (r == NULL);

  g_file_attribute_matcher_unref (m);
  g_vfs_enumeration_cache_free (c);
}

static void
test_max_time ()
{
  GVfsEnumerationCache *c;
  GList *l, *r;
  GFileAttributeMatcher *m;
  gint64 s;
  guint count;

  c = g_vfs_enumeration_cache_new (0, 1);
  m = g_file_attribute_matcher_new ("*");

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r != NULL);
  g_list_free_full (r, g_object_unref);

  /* timeout */
  g_usleep (1.1 * G_USEC_PER_SEC);
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r == NULL);

  /* garbage collector */
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 0);

  g_file_attribute_matcher_unref (m);
  g_vfs_enumeration_cache_free (c);
}

static void
test_max_count ()
{
  GVfsEnumerationCache *c;
  GList *l, *r;
  GFileAttributeMatcher *m;
  gint64 s;
  guint count;

  c = g_vfs_enumeration_cache_new (2, 0);
  m = g_file_attribute_matcher_new ("*");

  /* B, A */
  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("B"));
  g_vfs_enumeration_cache_set (c, "B", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 2);

  /* C, B */
  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("C"));
  g_vfs_enumeration_cache_set (c, "C", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r == NULL);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 2);

  /* B, C */
  r = g_vfs_enumeration_cache_find (c, "B", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r != NULL);
  g_list_free_full (r, g_object_unref);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 2);

  /* A, B */
  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  r = g_vfs_enumeration_cache_find (c, "C", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r == NULL);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 2);

  /* B, A (update) */
  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("B"));
  g_vfs_enumeration_cache_set (c, "B", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 2);

  /* C, B */
  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("C"));
  g_vfs_enumeration_cache_set (c, "C", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r == NULL);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 2);

  g_vfs_enumeration_cache_free (c);
  c = g_vfs_enumeration_cache_new (5, 0);

  /* B(4), A(1) */
  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("B"));
  g_vfs_enumeration_cache_set (c, "B", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 4);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 5);

  /* C(1), A(1) (size adjusted) */
  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("C"));
  g_vfs_enumeration_cache_set (c, "C", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  r = g_vfs_enumeration_cache_find (c, "B", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r == NULL);
  g_assert_cmpuint (g_vfs_enumeration_cache_get_count (c), ==, 2);

  g_file_attribute_matcher_unref (m);
  g_vfs_enumeration_cache_free (c);
}

static void
test_enable_disable ()
{
  GVfsEnumerationCache *c;
  GList *l, *r;
  GFileAttributeMatcher *m;
  gint64 s;
  guint count;

  c = g_vfs_enumeration_cache_new (0, 0);
  m = g_file_attribute_matcher_new ("*");
 
  g_vfs_enumeration_cache_disable (c);
  g_assert (g_vfs_enumeration_cache_is_disabled (c));
  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("B"));
  g_vfs_enumeration_cache_set (c, "B", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  g_vfs_enumeration_cache_enable (c);
  g_assert (!g_vfs_enumeration_cache_is_disabled (c));
  r = g_vfs_enumeration_cache_find (c, "B", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r == NULL);

  l = g_list_append (NULL, g_file_info_new ());
  s = g_vfs_enumeration_cache_insert (c, g_strdup ("A"));
  g_vfs_enumeration_cache_set (c, "A", l, g_file_attribute_matcher_new ("*"), G_FILE_QUERY_INFO_NONE, s, 1);
  g_vfs_enumeration_cache_disable (c);
  r = g_vfs_enumeration_cache_find (c, "A", m, G_FILE_QUERY_INFO_NONE, &count);
  g_assert (r != NULL);
  g_list_free_full (r, g_object_unref);
  g_vfs_enumeration_cache_enable (c);

  /* multiple */ 
  g_vfs_enumeration_cache_disable (c);
  g_vfs_enumeration_cache_disable (c);
  g_assert (g_vfs_enumeration_cache_is_disabled (c));
  g_vfs_enumeration_cache_enable (c);
  g_assert (g_vfs_enumeration_cache_is_disabled (c));
  g_vfs_enumeration_cache_enable (c);
  g_assert (!g_vfs_enumeration_cache_is_disabled (c));

  g_file_attribute_matcher_unref (m);
  g_vfs_enumeration_cache_free (c);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/enumerate_cache/new_free", test_new_free);
  g_test_add_func ("/enumerate_cache/insert_remove", test_insert_remove);
  g_test_add_func ("/enumerate_cache/find", test_find);
  g_test_add_func ("/enumerate_cache/attributes", test_attributes);
  g_test_add_func ("/enumerate_cache/flags", test_flags);
  g_test_add_func ("/enumerate_cache/max_time", test_max_time);
  g_test_add_func ("/enumerate_cache/max_count", test_max_count);
  g_test_add_func ("/enumerate_cache/enable_disable", test_enable_disable);
  
  return g_test_run ();
}
