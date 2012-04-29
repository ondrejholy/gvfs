#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <unistd.h>
#include <stdarg.h>

#define DATA_DIR                        "test-backend-archive-data/"
#define DATA_DIR_NONEXISTENT            "nonexistent"
#define DATA_DIR_FILE                   "file"
#define DATA_DIR_FILE_CONTENT           "e8neqdY5KJJC4ZizKfl87Rd9pPg4ZYj9LUqYZN8v14j494RdkL\n"
#define DATA_DIR_FILE_LENGTH            53
#define ARCHIVE_EMPTY                   DATA_DIR "empty.tar.gz"
#define ARCHIVE_BAD                     DATA_DIR "bad.tar.gz"
#define ARCHIVE_NONEXISTENT             DATA_DIR "nonexistent.tar.gz"
#define ARCHIVE_TEMP                    DATA_DIR "temp.tar.gz"
#define ARCHIVE_TEST                    DATA_DIR "test.tar.gz"
#define ARCHIVE_TEST_DIR                "dir"
#define ARCHIVE_TEST_DIR2               "dir2"
#define ARCHIVE_TEST_DIR2_FILE          "file"
#define ARCHIVE_TEST_DIR2_FILE_CONTENT  "SqzlLESC61vLYH8di3bxE37Meiu43G169kd12U727vi7D45hdh\n"
#define ARCHIVE_TEST_DIR2_FILE_LENGTH   53
#define ARCHIVE_TEST_FILE               "file"
#define ARCHIVE_TEST_FILE_CONTENT       "8MzZolJ6fHw73K445pElRl7w9bI2w789c4PeG78IuB6Z7GnQZ4\n"
#define ARCHIVE_TEST_FILE_LENGTH        53
#define ARCHIVE_TEST_FILE2              "file2"
#define ARCHIVE_TEST_FILE2_CONTENT      "60d40PBZule6890q6nrgW8O0OavO8jOeuQd9C3Bh80Fo62uO30\n"
#define ARCHIVE_TEST_FILE2_LENGTH       53
#define ARCHIVE_TEST_NONEXISTENT        "nonexistent"
#define ARCHIVE_TEST_NONEXISTENT2       "nonexistent2"

/* Data for callback. */ 
struct cb_data
{
  gboolean finished;
  GError *error;
};

/* Callback for unmount. */
static void
unmount_cb (GObject *object,
            GAsyncResult *result,
            gpointer user_data)
{
  struct cb_data *data = user_data;
  
  g_mount_unmount_with_operation_finish (G_MOUNT (object), 
                                         result, 
                                         &data->error);
  
  data->finished = TRUE;
}

/* Callback for timeout. */
static gboolean 
timeout_cb (gpointer user_data)
{
  struct cb_data *data = user_data;
  
  data->finished = TRUE;
  
  return FALSE;
}

/* Unmount the archive. */
static GError *
archive_unmount (GFile *file)
{
  struct cb_data data = {FALSE, NULL};
  GMount *mount;
  
  mount = g_file_find_enclosing_mount (file, NULL, &data.error);
  if (mount == NULL)
    return data.error;
  
  g_mount_unmount_with_operation (mount, 
                                  G_MOUNT_UNMOUNT_NONE, 
                                  NULL, 
                                  NULL, 
                                  unmount_cb, 
                                  &data);
  
  /* Wait for result. */
  while (!data.finished) 
    g_main_context_iteration (NULL, TRUE);  
  
  g_object_unref (mount);
  
  /* Wait for definitive unmount. */
  data.finished = FALSE;
  g_timeout_add (2000, timeout_cb, &data);
  while (!data.finished) 
    g_main_context_iteration (NULL, TRUE);  
      
  return data.error;
}

/* Callback for mount. */
static void
mount_cb (GObject *object,
          GAsyncResult *result,
          gpointer user_data)
{
  struct cb_data *data = user_data;
  
  g_file_mount_enclosing_volume_finish (G_FILE (object), 
                                        result, 
                                        &data->error);
  
  data->finished = TRUE;
}

/* (Re)Mount the archive. */
static GError *
archive_mount (GFile *file)
{
  struct cb_data data = {FALSE, NULL};
  
  /* Try to unmount first. */
  data.error = archive_unmount (file);
  g_clear_error (&data.error);
  
  g_file_mount_enclosing_volume (file,
                                 G_MOUNT_MOUNT_NONE,
                                 NULL,
                                 NULL,
                                 mount_cb,
                                 &data);
  
  /* Wait for result. */
  while (!data.finished)
    g_main_context_iteration (NULL, TRUE);  
  
  /* Wait for definitive mount. */
  data.finished = FALSE;
  g_timeout_add (2000, timeout_cb, &data);
  while (!data.finished) 
    g_main_context_iteration (NULL, TRUE);  
  
  return data.error;
}

/* Make correct uri for mount. */
static char *
archive_uri (const char *name)
{
  GFile *file;
  char *uri;
  char *uri_escaped;
  char *uri_escaped_escaped;
  char *archive_uri;
  
  file = g_file_new_for_path (name);
  
  uri = g_file_get_uri (file);
  uri_escaped = g_uri_escape_string (uri, NULL, TRUE);
  uri_escaped_escaped = g_uri_escape_string (uri_escaped, NULL, TRUE);  
  archive_uri = g_strconcat ("archive://", uri_escaped_escaped, "/", NULL);
  
  g_object_unref (file);
  g_free (uri);
  g_free (uri_escaped);
  g_free (uri_escaped_escaped);
  
  return archive_uri;
}

/* Make a temporary archive file. */
static GFile *
archive_temp_new (const char *name)
{
  GError *error = NULL;
  GFile *file;
  GFile *file_temp;
  GFile *archive;
  char *uri;
  
  file = g_file_new_for_path (name);
  file_temp = g_file_new_for_path (ARCHIVE_TEMP);
  
  g_file_copy (file, 
               file_temp,
               G_FILE_COPY_OVERWRITE,
               NULL,
               NULL,
               NULL,
               &error);
  g_assert_no_error (error);
  
  uri = archive_uri (ARCHIVE_TEMP);
  archive = g_file_new_for_uri (uri);
  
  g_free (uri);
  g_object_unref (file);
  g_object_unref (file_temp);
  
  return archive;
}

/* Delete the temporary archive file. */
static void
archive_temp_free (GFile *file)
{
  GError *error = NULL;
  GFile *file_temp;
  
  file_temp = g_file_new_for_path (ARCHIVE_TEMP);
  g_file_delete (file_temp, NULL, &error);
  g_assert_no_error (error);
  
  g_object_unref (file_temp);
  g_object_unref (file);
}

/* Check getting info of file. */
static GError *
check_info (GFile *parent, char *child)
{
  GFile *file;
  GFileInfo *info;
  GError *error = NULL;
  
  file = g_file_get_child (parent, child);
  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_NAME,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            &error);
  if (info != NULL)
    g_object_unref (info);
  g_object_unref (file);
  
  return error;
}

/* Check if parent has children with names (last must be NULL). */
static GError *
check_children (GFile *parent, ...)
{
  va_list list;
  char *child;
  GFileInfo *info;
  GFileEnumerator *enumerator;
  GError *error = NULL;
  
  va_start (list, parent);  
  
  enumerator = g_file_enumerate_children (parent,
                                          G_FILE_ATTRIBUTE_STANDARD_NAME,
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL,
                                          &error);
  if (error != NULL)
    {
      va_end (list);
      
      return error;
    }
  
  do 
    {
      child = va_arg (list, char *);
      
      info = g_file_enumerator_next_file (enumerator, NULL, &error);
      if (info != NULL)
        {
          g_assert_cmpstr (g_file_info_get_name (info), == , child);
          g_object_unref (info);
        }
    }
  while (child != NULL && error != NULL);
  
  va_end (list);
  g_object_unref (enumerator);
  
  return error;
}

/* Check content of a child file. */
static GError *
check_content (GFile *parent, char *child, char *content, int length)
{
  GFile *file;
  GFileInputStream *stream;
  GError *error = NULL;
  gsize read;
  char *buffer;
  
  file = g_file_get_child (parent, child);
  stream = g_file_read (file, NULL, &error);
  if (error != NULL)
    {
      g_object_unref (file);
      
      return error;
    }
  
  buffer = g_slice_alloc (length * sizeof (char));
  g_input_stream_read_all (G_INPUT_STREAM (stream),
                           buffer,
                           length,
                           &read,
                           NULL,
                           &error);
  buffer [length - 1] = '\0';
  if (error == NULL)
    g_assert_cmpstr (buffer, == , content);  
  
  g_slice_free1 (length * sizeof (char), buffer);
  g_object_unref (stream);
  g_object_unref (file);
  
  return error;
}

/* Try rename of a file. */
static GError *
try_rename (GFile *parent, char *child, char *name)
{
  GFile *file;
  GFile *renamed;
  GError *error = NULL;
  
  file = g_file_get_child (parent, child);
  renamed = g_file_set_display_name (file,
                                     name,
                                     NULL,
                                     &error);
  if (renamed != NULL)
    g_object_unref (renamed);
  g_object_unref (file);
  
  return error;
}

/* Try make a dir. */
static GError *
try_make_directory (GFile *parent, char *name)
{
  GFile *file;
  GError *error = NULL;
  
  file = g_file_get_child (parent, name);
  g_file_make_directory (file, NULL, &error);
  g_object_unref (file);
  
  return error;
}

/* Try delete a file. */
static GError *
try_delete (GFile *parent, char *name)
{
  GFile *file;
  GError *error = NULL;
  
  file = g_file_get_child (parent, name);
  g_file_delete (file, NULL, &error);
  g_object_unref (file);
  
  return error;
}

/* Try move a file inside archive. */
static GError *
try_move (GFile *parent, char *child, char *name, GFileCopyFlags flags)
{
  GFile *source;
  GFile *destination;
  GError *error = NULL;
  
  source = g_file_get_child (parent, child);
  destination = g_file_get_child (parent, name);
  g_file_move (source,
               destination,
               flags,
               NULL,
               NULL,
               NULL,
               &error);
  
  g_object_unref (source);
  g_object_unref (destination);
  
  return error;
}

/* Try push a file. */
static GError *
try_push (GFile *parent, 
          char *child, 
          GFile *source, 
          gboolean move, 
          GFileCopyFlags flags)
{
  GFile *destination;
  GError *error = NULL;
  
  destination = g_file_get_child (parent, child);
  if (move)
    {
      g_file_move (source,
                   destination,
                   flags,
                   NULL,
                   NULL,
                   NULL,
                   &error);
    }
  else
    g_file_copy (source,
                 destination,
                 flags,
                 NULL,
                 NULL,
                 NULL,
                 &error);
  
  g_object_unref (destination);
  
  return error;
}

/*** TEST SUITS ***/

/* Mount test suite. */
static void
test_mount ()
{
  GFile *archive;
  GError *error;
  char *uri;
  
  g_test_message ("bad");
  archive = archive_temp_new (ARCHIVE_BAD);
  error = archive_mount (archive);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_clear_error (&error);
  archive_temp_free (archive);
  
  g_test_message ("empty");
  archive = archive_temp_new (ARCHIVE_EMPTY);
  error = archive_mount (archive);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTABLE_FILE);
  g_clear_error (&error);
  archive_temp_free (archive);
  
  g_test_message ("nonexistent");
  uri = archive_uri (ARCHIVE_NONEXISTENT);
  archive = g_file_new_for_uri (uri);
  g_free (uri);
  error = archive_mount (archive);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_clear_error (&error);
  g_object_unref (archive);
  
  g_test_message ("none");
  archive = g_file_new_for_uri ("archive:////");
  error = archive_mount (archive);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error (&error);
  g_object_unref (archive); 

  g_test_message ("test");
  archive = archive_temp_new (ARCHIVE_TEST);
  g_assert_no_error (archive_mount (archive));
  g_assert_no_error (archive_unmount (archive));
  archive_temp_free (archive);
}

/* Info test suite. */
static void
test_query_info ()
{
  GFile *archive;
  GError *error = NULL;
  
  archive = archive_temp_new (ARCHIVE_TEST);
  g_assert_no_error (archive_mount (archive));
  
  g_test_message ("nonexistent"); 
  error = check_info (archive, ARCHIVE_TEST_NONEXISTENT);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_clear_error (&error);

  g_test_message ("root"); 
  error = check_info (archive, ".");
  g_assert_no_error (error);
  
  g_test_message ("file"); 
  error = check_info (archive, ARCHIVE_TEST_FILE);
  g_assert_no_error (error);
  
  g_test_message ("dir"); 
  error = check_info (archive, ARCHIVE_TEST_DIR);
  g_assert_no_error (error);
  
  g_assert_no_error (archive_unmount (archive));
  archive_temp_free (archive);
}

/* Enumerate test suite. */
static void
test_enumerate ()
{
  GFile *archive;
  GFile *file;
  GError *error = NULL;
  
  archive = archive_temp_new (ARCHIVE_TEST);
  g_assert_no_error (archive_mount (archive));
  
  g_test_message ("file");
  file = g_file_get_child (archive, ARCHIVE_TEST_FILE);
  error = check_children (file, NULL);                                        
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY);
  g_clear_error (&error);
  g_object_unref (file);
    
  g_test_message ("nonexistent");
  file = g_file_get_child (archive, ARCHIVE_TEST_NONEXISTENT);
  error = check_children (file, NULL);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_clear_error (&error);
  g_object_unref (file);
  
  g_test_message ("root");
  error = check_children (archive, 
                          ARCHIVE_TEST_FILE2, 
                          ARCHIVE_TEST_FILE, 
                          ARCHIVE_TEST_DIR, 
                          ARCHIVE_TEST_DIR2, 
                          NULL);
  g_assert_no_error (error);
  
  g_test_message ("dir");
  file = g_file_get_child (archive, ARCHIVE_TEST_DIR);
  error = check_children (file, NULL);
  g_assert_no_error (error);
  g_object_unref (file);
  
  g_test_message ("dir2");
  file = g_file_get_child (archive, ARCHIVE_TEST_DIR2);
  error = check_children (file, ARCHIVE_TEST_DIR2_FILE, NULL);
  g_assert_no_error (error);
  g_object_unref (file);
  
  g_assert_no_error (archive_unmount (archive));
  archive_temp_free (archive);
}

/* Read test suite. */
static void
test_read ()
{
  GFile *archive;
  GError *error = NULL;
  
  archive = archive_temp_new (ARCHIVE_TEST);
  g_assert_no_error (archive_mount (archive));
  
  g_test_message ("root");
  error = check_content (archive, ".", NULL, 0);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY);
  g_clear_error (&error);
  
  g_test_message ("dir");
  error = check_content (archive, ARCHIVE_TEST_DIR, NULL, 0);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY);
  g_clear_error (&error);
  
  g_test_message ("nonexistent");  
  error = check_content (archive, ARCHIVE_TEST_NONEXISTENT, NULL, 0);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_clear_error (&error);
  
  g_test_message ("file");
  error = check_content (archive, 
                         ARCHIVE_TEST_FILE, 
                         ARCHIVE_TEST_FILE_CONTENT, 
                         ARCHIVE_TEST_FILE_LENGTH);
  g_assert_no_error (error);
  
  g_assert_no_error (archive_unmount (archive));
  archive_temp_free (archive);
}

/* Rename test suite. */
static void
test_set_display_name ()
{
  GFile *archive;
  GFile *file;
  GError *error = NULL;
  
  archive = archive_temp_new (ARCHIVE_TEST);
  g_assert_no_error (archive_mount (archive));
  
  g_test_message ("root -> nonexistent");
  error = try_rename (archive, ".", ARCHIVE_TEST_NONEXISTENT);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_clear_error (&error);
  
  g_test_message ("nonexistent -> nonexistent2"); 
  error = try_rename (archive, 
                      ARCHIVE_TEST_NONEXISTENT, 
                      ARCHIVE_TEST_NONEXISTENT);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_clear_error (&error);

  g_test_message ("dir -> file"); 
  error = try_rename (archive, 
                      ARCHIVE_TEST_DIR, 
                      ARCHIVE_TEST_FILE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);

  g_test_message ("dir -> dir2"); 
  error = try_rename (archive, 
                      ARCHIVE_TEST_DIR, 
                      ARCHIVE_TEST_DIR2);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);
  
  g_test_message ("file -> dir"); 
  error = try_rename (archive, 
                      ARCHIVE_TEST_FILE, 
                      ARCHIVE_TEST_DIR);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);

  g_test_message ("file -> file2"); 
  error = try_rename (archive, 
                      ARCHIVE_TEST_FILE, 
                      ARCHIVE_TEST_FILE2);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);
    
    
  g_test_message ("file -> none");
  error = try_rename (archive, 
                      ARCHIVE_TEST_FILE, 
                      "");
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error (&error);
  
  g_test_message ("file -> dir/nonexistent");
  error = try_rename (archive, 
                      ARCHIVE_TEST_FILE, 
                      ARCHIVE_TEST_DIR "/" ARCHIVE_TEST_NONEXISTENT);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error (&error);
  
  g_test_message ("file -> file"); 
  error = try_rename (archive, 
                      ARCHIVE_TEST_FILE, 
                      ARCHIVE_TEST_FILE);
  g_assert_no_error (error);
  g_assert_no_error (archive_mount (archive));
  error = check_children (archive, 
                          ARCHIVE_TEST_FILE2, 
                          ARCHIVE_TEST_FILE, 
                          ARCHIVE_TEST_DIR, 
                          ARCHIVE_TEST_DIR2, 
                          NULL);
  g_assert_no_error (error);
  error = check_content (archive, 
                         ARCHIVE_TEST_FILE, 
                         ARCHIVE_TEST_FILE_CONTENT, 
                         ARCHIVE_TEST_FILE_LENGTH);
  g_assert_no_error (error);
  
  g_test_message ("file -> nonexistent");
  error = try_rename (archive, 
                      ARCHIVE_TEST_FILE, 
                      ARCHIVE_TEST_NONEXISTENT);
  g_assert_no_error (error);
  g_assert_no_error (archive_mount (archive));
  error = check_children (archive, 
                          ARCHIVE_TEST_FILE2,
                          ARCHIVE_TEST_NONEXISTENT, 
                          ARCHIVE_TEST_DIR, 
                          ARCHIVE_TEST_DIR2, 
                          NULL);
  g_assert_no_error (error);
  error = check_content (archive, 
                         ARCHIVE_TEST_NONEXISTENT, 
                         ARCHIVE_TEST_FILE_CONTENT, 
                         ARCHIVE_TEST_FILE_LENGTH);
  g_assert_no_error (error);
  
  g_test_message ("dir2 -> nonexistent2"); 
  error = try_rename (archive, 
                      ARCHIVE_TEST_DIR, 
                      ARCHIVE_TEST_NONEXISTENT2);
  g_assert_no_error (error);
  g_assert_no_error (archive_mount (archive));
  error = check_children (archive, 
                          ARCHIVE_TEST_FILE2, 
                          ARCHIVE_TEST_NONEXISTENT,
                          ARCHIVE_TEST_DIR, 
                          ARCHIVE_TEST_NONEXISTENT2, 
                          NULL);
  g_assert_no_error (error);
  file = g_file_get_child (archive, ARCHIVE_TEST_DIR2);
  error = check_children (file, ARCHIVE_TEST_DIR2_FILE, NULL);
  g_assert_no_error (error);  
  error = check_content (file, 
                         ARCHIVE_TEST_DIR2_FILE, 
                         ARCHIVE_TEST_DIR2_FILE_CONTENT, 
                         ARCHIVE_TEST_DIR2_FILE_LENGTH);
  g_object_unref (file);
  
  g_assert_no_error (archive_unmount (archive));
  archive_temp_free (archive); 
}

/* Make directory test suite. */
static void
test_make_directory ()
{
  GFile *archive;
  GFile *file;
  GError *error = NULL;
  
  archive = archive_temp_new (ARCHIVE_TEST);
  g_assert_no_error (archive_mount (archive));
  
  g_test_message ("root");
  error = try_make_directory (archive, ".");
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);
  
  g_test_message ("dir");
  error = try_make_directory (archive, ARCHIVE_TEST_DIR);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);
  
  g_test_message ("file");
  error = try_make_directory (archive, ARCHIVE_TEST_FILE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);
  
  g_test_message ("nonexistent");
  error = try_make_directory (archive, ARCHIVE_TEST_NONEXISTENT);
  g_assert_no_error (error);
  g_assert_no_error (archive_mount (archive));
  error = check_children (archive, 
                          ARCHIVE_TEST_NONEXISTENT,
                          ARCHIVE_TEST_FILE, 
                          ARCHIVE_TEST_FILE2, 
                          ARCHIVE_TEST_DIR, 
                          ARCHIVE_TEST_DIR2,
                          NULL);
  g_assert_no_error (error);
    
  g_test_message ("nonexistent2/nonexistent");
  error = try_make_directory (archive, 
            ARCHIVE_TEST_NONEXISTENT2 "/" ARCHIVE_TEST_NONEXISTENT);
  g_assert_no_error (error);
  g_assert_no_error (archive_mount (archive));
  file = g_file_get_child (archive, ARCHIVE_TEST_NONEXISTENT2);
  error = check_children (file, ARCHIVE_TEST_NONEXISTENT, NULL); 
  g_assert_no_error (error);
  g_object_unref (file);
  
  g_assert_no_error (archive_unmount (archive));
  archive_temp_free (archive); 
}

/* Delete test suite. */
static void
test_delete ()
{
  GFile *archive;
  GError *error = NULL;
  
  archive = archive_temp_new (ARCHIVE_TEST);
  g_assert_no_error (archive_mount (archive));
  
  g_test_message ("root");
  error = try_delete (archive, ".");
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_clear_error (&error);
  
  g_test_message ("nonexistent");
  error = try_delete (archive, ARCHIVE_TEST_NONEXISTENT);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_clear_error (&error);
  
  g_test_message ("dir");
  error = try_delete (archive, ARCHIVE_TEST_DIR);
  g_assert_no_error (error);
  error = check_children (archive, 
                          ARCHIVE_TEST_FILE2, 
                          ARCHIVE_TEST_FILE, 
                          ARCHIVE_TEST_DIR2, 
                          NULL); 
  g_assert_no_error (error);
  
  g_test_message ("dir2 (recurse)");
  error = try_delete (archive, ARCHIVE_TEST_DIR2);
  g_assert_no_error (error);
  error = check_children (archive, 
                          ARCHIVE_TEST_FILE2, 
                          ARCHIVE_TEST_FILE, 
                          NULL); 
  g_assert_no_error (error);
  
  g_test_message ("file");
  error = try_delete (archive, ARCHIVE_TEST_FILE);
  g_assert_no_error (error);
  error = check_children (archive, ARCHIVE_TEST_FILE2, NULL); 
  g_assert_no_error (error);
  
  g_assert_no_error (archive_unmount (archive));
  archive_temp_free (archive); 
}

/* Move test suite. */
static void
test_move ()
{
  GFile *archive;
  GFile *file;
  GError *error = NULL;
  
  archive = archive_temp_new (ARCHIVE_TEST);
  g_assert_no_error (archive_mount (archive));
  
  g_test_message ("root -> dir (overwrite)");
  error = try_move (archive, 
                    ".", 
                    ARCHIVE_TEST_NONEXISTENT, 
                    G_FILE_COPY_OVERWRITE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE);
  g_clear_error (&error);
  
  g_test_message ("dir -> file");
  error = try_move (archive, 
                    ARCHIVE_TEST_DIR, 
                    ARCHIVE_TEST_FILE, 
                    G_FILE_COPY_NONE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);

  g_test_message ("dir -> dir2");
  error = try_move (archive, 
                    ARCHIVE_TEST_DIR, 
                    ARCHIVE_TEST_DIR2, 
                    G_FILE_COPY_NONE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);

  g_test_message ("dir -> dir2 (overwrite)");
  error = try_move (archive, 
                    ARCHIVE_TEST_DIR, 
                    ARCHIVE_TEST_DIR2, 
                    G_FILE_COPY_OVERWRITE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_WOULD_MERGE);
  g_clear_error (&error);

  g_test_message ("file -> dir");
  error = try_move (archive, 
                    ARCHIVE_TEST_FILE, 
                    ARCHIVE_TEST_DIR, 
                    G_FILE_COPY_NONE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);

  g_test_message ("file -> file2");
  error = try_move (archive, 
                    ARCHIVE_TEST_FILE, 
                    ARCHIVE_TEST_FILE2, 
                    G_FILE_COPY_NONE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);
  
  g_test_message ("nonexistent -> nonexistent2");
  error = try_move (archive, 
                    ARCHIVE_TEST_NONEXISTENT, 
                    ARCHIVE_TEST_NONEXISTENT2,
                    G_FILE_COPY_NONE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_clear_error (&error);
  
  g_test_message ("nonexistent -> nonexistent2 (overwrite)");
  error = try_move (archive, 
                    ARCHIVE_TEST_NONEXISTENT, 
                    ARCHIVE_TEST_NONEXISTENT2,
                    G_FILE_COPY_OVERWRITE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_clear_error (&error);
  
  g_test_message ("file -> file");
  error = try_move (archive, 
                    ARCHIVE_TEST_FILE, 
                    ARCHIVE_TEST_FILE, 
                    G_FILE_COPY_NONE);
  g_assert_no_error (error);
  g_assert_no_error (archive_mount (archive));
  error = check_children (archive, 
                          ARCHIVE_TEST_FILE2, 
                          ARCHIVE_TEST_FILE, 
                          ARCHIVE_TEST_DIR, 
                          ARCHIVE_TEST_DIR2,
                          NULL);
  g_assert_no_error (error);  
  error = check_content (archive, 
                         ARCHIVE_TEST_FILE, 
                         ARCHIVE_TEST_FILE_CONTENT, 
                         ARCHIVE_TEST_FILE_LENGTH);
  g_assert_no_error (error);
  
  g_test_message ("file -> dir (overwrite)");
  error = try_move (archive, 
                    ARCHIVE_TEST_FILE, 
                    ARCHIVE_TEST_DIR, 
                    G_FILE_COPY_OVERWRITE);
  g_assert_no_error (error);
  g_assert_no_error (archive_mount (archive));
  error = check_children (archive, 
                          ARCHIVE_TEST_FILE2,
                          ARCHIVE_TEST_DIR,
                          ARCHIVE_TEST_DIR2,
                          NULL);
  g_assert_no_error (error);
  error = check_content (archive, 
                         ARCHIVE_TEST_DIR, 
                         ARCHIVE_TEST_FILE_CONTENT, 
                         ARCHIVE_TEST_FILE_LENGTH);
  g_assert_no_error (error);
  
  g_test_message ("file -> file2 (overwrite)");
  error = try_move (archive, 
                    ARCHIVE_TEST_DIR, 
                    ARCHIVE_TEST_FILE2, 
                    G_FILE_COPY_OVERWRITE);
  g_assert_no_error (error);
  g_assert_no_error (archive_mount (archive));
  error = check_children (archive, 
                          ARCHIVE_TEST_FILE2,
                          ARCHIVE_TEST_DIR2,
                          NULL);
  g_assert_no_error (error);
  error = check_content (archive, 
                         ARCHIVE_TEST_FILE2, 
                         ARCHIVE_TEST_FILE_CONTENT, 
                         ARCHIVE_TEST_FILE_LENGTH);
  g_assert_no_error (error);
  
  g_test_message ("dir2 -> file2 (overwrite)");
  error = try_move (archive, 
                    ARCHIVE_TEST_DIR2, 
                    ARCHIVE_TEST_FILE2, 
                    G_FILE_COPY_OVERWRITE);
  g_assert_no_error (error);
  g_assert_no_error (archive_mount (archive));
  error = check_children (archive,
                          ARCHIVE_TEST_FILE2,
                          NULL);
  g_assert_no_error (error);
  file = g_file_get_child (archive, ARCHIVE_TEST_FILE2);
  error = check_children (file, ARCHIVE_TEST_DIR2_FILE, NULL);
  g_assert_no_error (error);  
  error = check_content (file, 
                         ARCHIVE_TEST_DIR2_FILE, 
                         ARCHIVE_TEST_DIR2_FILE_CONTENT, 
                         ARCHIVE_TEST_DIR2_FILE_LENGTH);
  g_assert_no_error (error);
  g_object_unref (file); 
  
  g_assert_no_error (archive_unmount (archive));
  archive_temp_free (archive);
}

/* Push test suite. */
static void
test_push ()
{
  GFile *archive;
  GFile *file;
  GError *error = NULL;
  
  archive = archive_temp_new (ARCHIVE_TEST);
  g_assert_no_error (archive_mount (archive));
  
  g_test_message ("dir -> nonexistent");
  file = g_file_new_for_path (DATA_DIR);
  error = try_push (archive, 
                    ARCHIVE_TEST_NONEXISTENT, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_NONE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE);
  g_clear_error (&error);
  
  g_test_message ("dir -> nonexistent (overwrite)");
  file = g_file_new_for_path (DATA_DIR);
  error = try_push (archive, 
                    ARCHIVE_TEST_NONEXISTENT, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_OVERWRITE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE);
  g_clear_error (&error);
  
  g_test_message ("dir -> file");
  file = g_file_new_for_path (DATA_DIR);
  error = try_push (archive, 
                    ARCHIVE_TEST_FILE, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_NONE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);
  
  g_test_message ("dir -> file (overwrite)");
  file = g_file_new_for_path (DATA_DIR);
  error = try_push (archive, 
                    ARCHIVE_TEST_FILE, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_OVERWRITE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE);
  g_clear_error (&error);
  
  g_test_message ("dir -> dir");
  file = g_file_new_for_path (DATA_DIR);
  error = try_push (archive, 
                    ARCHIVE_TEST_DIR, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_NONE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);
  
  g_test_message ("dir -> dir (overwrite)");
  file = g_file_new_for_path (DATA_DIR);
  error = try_push (archive, 
                    ARCHIVE_TEST_DIR, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_OVERWRITE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_WOULD_MERGE);
  g_clear_error (&error);
  
  g_test_message ("file -> dir");
  file = g_file_new_for_path (DATA_DIR DATA_DIR_FILE);
  error = try_push (archive, 
                    ARCHIVE_TEST_DIR, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_NONE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);
  
  g_test_message ("file -> file");
  file = g_file_new_for_path (DATA_DIR DATA_DIR_FILE);
  error = try_push (archive, 
                    ARCHIVE_TEST_FILE, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_NONE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_clear_error (&error);
  
  g_test_message ("nonexistent -> nonexistent");
  file = g_file_new_for_path (DATA_DIR DATA_DIR_NONEXISTENT);
  error = try_push (archive, 
                    ARCHIVE_TEST_NONEXISTENT, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_NONE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_clear_error (&error);
  
  g_test_message ("nonexistent -> nonexistent (overwrite)");
  file = g_file_new_for_path (DATA_DIR DATA_DIR_NONEXISTENT);
  error = try_push (archive, 
                    ARCHIVE_TEST_NONEXISTENT, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_NONE);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_clear_error (&error);
  
  g_test_message ("file -> file (overwrite)");
  file = g_file_new_for_path (DATA_DIR DATA_DIR_FILE);
  error = try_push (archive, 
                    ARCHIVE_TEST_FILE, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_OVERWRITE);
  g_assert_no_error (error);
  g_assert_no_error (archive_mount (archive));
  error = check_children (archive,
                          ARCHIVE_TEST_FILE,
                          ARCHIVE_TEST_FILE2,
                          ARCHIVE_TEST_DIR,
                          ARCHIVE_TEST_DIR2,
                          NULL);
  g_assert_no_error (error);
  error = check_content (archive, 
                         ARCHIVE_TEST_FILE, 
                         DATA_DIR_FILE_CONTENT, 
                         DATA_DIR_FILE_LENGTH);
  g_assert_no_error (error);
  g_object_unref (file); 
  
  g_test_message ("file -> dir (overwrite)");
  file = g_file_new_for_path (DATA_DIR DATA_DIR_FILE);
  error = try_push (archive, 
                    ARCHIVE_TEST_DIR, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_OVERWRITE);
  g_assert_no_error (error);
  g_assert_no_error (archive_mount (archive));
  error = check_children (archive,
                          ARCHIVE_TEST_DIR,
                          ARCHIVE_TEST_FILE,
                          ARCHIVE_TEST_FILE2,
                          ARCHIVE_TEST_DIR2,
                          NULL);
  g_assert_no_error (error);
  error = check_content (archive, 
                         ARCHIVE_TEST_DIR, 
                         DATA_DIR_FILE_CONTENT, 
                         DATA_DIR_FILE_LENGTH);
  g_assert_no_error (error);
  g_object_unref (file); 
  
  g_test_message ("file -> nonexistent");
  file = g_file_new_for_path (DATA_DIR DATA_DIR_FILE);
  error = try_push (archive, 
                    ARCHIVE_TEST_NONEXISTENT, 
                    file, 
                    FALSE, 
                    G_FILE_COPY_OVERWRITE);
  g_assert_no_error (error);
  g_assert_no_error (archive_mount (archive));
  error = check_children (archive,
                          ARCHIVE_TEST_NONEXISTENT,
                          ARCHIVE_TEST_DIR,
                          ARCHIVE_TEST_FILE,
                          ARCHIVE_TEST_FILE2,
                          ARCHIVE_TEST_DIR2,
                          NULL);
  g_assert_no_error (error);
  error = check_content (archive, 
                         ARCHIVE_TEST_NONEXISTENT, 
                         DATA_DIR_FILE_CONTENT, 
                         DATA_DIR_FILE_LENGTH);
  g_assert_no_error (error);
  g_object_unref (file); 
  
  g_assert_no_error (archive_unmount (archive));
  archive_temp_free (archive);
}

int
main (int argc, char *argv[])
{
  g_type_init ();
  
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/archive/mount", test_mount);
  g_test_add_func ("/archive/query_info", test_query_info);
  g_test_add_func ("/archive/enumerate", test_enumerate);
  g_test_add_func ("/archive/read", test_read);
  g_test_add_func ("/archive/set_display_name", test_set_display_name); 
  g_test_add_func ("/archive/make_directory", test_make_directory);
  g_test_add_func ("/archive/delete", test_delete);
  g_test_add_func ("/archive/move", test_move);
  g_test_add_func ("/archive/push", test_push);
  
  return g_test_run ();
}
