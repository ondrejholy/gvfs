#include <glib.h>
#include <gio/gio.h>
#include <math.h>

#define REMOUNT_WAIT 1800000 /* Time between unmount and mount */

static GMainLoop *main_loop;
static gint iterations;
static GFile *file;
static gint i;
static GArray *first_values;
static GArray *second_values;
static gint test_name;

/* Operation mode */
enum
{
  QUERYINFO,
  ENUMERATION,
  NAUTILUS
};

static void mount_cb (GObject *object, GAsyncResult *res, gpointer data);

static void
stats (GArray *array)
{
  gint j;
  double sum;
  double average;
  double deviation;

  sum = 0;
  for (j = 0; j < (iterations - i); j++)
  {
    sum += g_array_index (array, gint64, j);
  }
  average = sum / (iterations - i);

  sum = 0;
  for (j = 0; j < (iterations - i); j++)
  {
    sum += pow (g_array_index (array, gint64, j) - average, 2);
  }
  deviation = sqrt (sum / (iterations - 1));
  g_print ("%lf usec (standard deviation: %lf)\n", average, deviation);
}

static gboolean
operation (GArray *array)
{
  GError *error = NULL;
  gint64 value;
  GFileInfo *info;
  GFileEnumerator *enumerator;
  GFileMonitor *monitor;

  value = g_get_monotonic_time ();
  switch (test_name)
  {
    case ENUMERATION:
      enumerator = g_file_enumerate_children (file, "*", G_FILE_QUERY_INFO_NONE, NULL, &error);
      if (error) break;
      info = g_file_enumerator_next_file (enumerator, NULL, &error);
      g_clear_object (&enumerator);
      g_clear_object (&info);
      break;
    case QUERYINFO:
      info = g_file_query_info (file, "*", G_FILE_QUERY_INFO_NONE, NULL, &error);
      g_clear_object (&info);
      break;
    case NAUTILUS:
      info = g_file_query_info (file, "standard::*,access::*,mountable::*,time::*,unix::*,owner::*,selinux::*,thumbnail::*,id::filesystem,trash::orig-path,trash::deletion-date,metadata::*", G_FILE_QUERY_INFO_NONE, NULL, &error);
      g_clear_object (&info);
      if (error) break;

      info = g_file_query_filesystem_info (file, "filesystem::readonly,filesystem::use-preview", NULL, &error);
      g_clear_object (&info);
      if (error) break;

      monitor = g_file_monitor_directory (file, 0, NULL, &error);
      g_clear_object (&monitor);
      if (error) g_clear_error (&error);

      enumerator = g_file_enumerate_children (file, "standard::*,access::*,mountable::*,time::*,unix::*,owner::*,selinux::*,thumbnail::*,id::filesystem,trash::orig-path,trash::deletion-date,metadata::*", G_FILE_QUERY_INFO_NONE, NULL, &error);
      if (error) break;
      info = g_file_enumerator_next_file (enumerator, NULL, &error);
      g_clear_object (&info);
      g_clear_object (&enumerator);
      if (error) break;

      info = g_file_query_info (file, "standard::*,access::*,mountable::*,time::*,unix::*,owner::*,selinux::*,thumbnail::*,id::filesystem,trash::orig-path,trash::deletion-date,metadata::*", G_FILE_QUERY_INFO_NONE, NULL, &error);
      g_clear_object (&info);
      if (error) break;

      info = g_file_query_info (file, "standard::*,access::*,mountable::*,time::*,unix::*,owner::*,selinux::*,thumbnail::*,id::filesystem,trash::orig-path,trash::deletion-date,metadata::*", G_FILE_QUERY_INFO_NONE, NULL, &error);
      g_clear_object (&info);
      break;
  }
  value = g_get_monotonic_time () - value;
  g_array_append_val (array, value);
  if (error)
  {
    g_printerr ("%s\n", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  return TRUE;
}

static gboolean
test ()
{
  gboolean retval;

  /* First access */
  retval = operation (first_values);
  if (!retval)
    return FALSE;

  /* Seconds access */
  retval = operation (second_values);
  if (!retval)
    return FALSE;

  /* Decrease number of iterations */
  i--;

  return TRUE;
}

static void
unmount_cb (GObject *object, GAsyncResult *res, gpointer data)
{
  GError *error = NULL;

  /* Unmount finish */
  g_mount_unmount_with_operation_finish ((GMount *)object, res, &error);
  if (error)
  {
    g_printerr ("%s\n", error->message);
    g_clear_error (&error);
  }

  /* Next or finish */
  if (i > 0)
  {
    /* Be sure unmount is done */
    g_usleep (REMOUNT_WAIT);
    g_file_mount_enclosing_volume (file, 0, NULL, NULL, mount_cb, NULL);
  }
  else
  {
    g_main_loop_quit (main_loop);
  }
}

static void
mount_cb (GObject *object, GAsyncResult *res, gpointer data)
{
  GError *error = NULL;
  GMount *mount = NULL;
  GFileInfo *info;
  gboolean retval;

  /* Mount finish */
  g_file_mount_enclosing_volume_finish ((GFile *)object, res, &error);
  if (error)
  {
    g_printerr ("%s\n", error->message);
    g_clear_error (&error);
    g_main_loop_quit (main_loop);
    return;
  }

  /* Get mount */
  mount = g_file_find_enclosing_mount (file, NULL, &error);
  if (error)
  {
    g_printerr ("%s\n", error->message);
    g_clear_error (&error);
    g_main_loop_quit (main_loop);
    return;
  }
  if (!mount)
  {
    g_main_loop_quit (main_loop);
    return;
  }

  /* Be sure everything is initialized */
  info = g_file_query_filesystem_info (file, "*", NULL, &error);
  if (error)
  {
    g_printerr ("%s\n", error->message);
    g_clear_error (&error);
    g_main_loop_quit (main_loop);
    return;
  }
  g_object_unref (info);

  /* Run test */
  retval = test ();
  if (!retval)
    return;

  /* Print stats */
  g_print ("stats for iteration: %d\n", iterations - i);

  stats (first_values);
  stats (second_values);

  g_print ("\n");

  /* Unmount */
  g_mount_unmount_with_operation (mount, G_MOUNT_UNMOUNT_FORCE, NULL, NULL, unmount_cb, NULL);
}

int
main (int argc, char *argv[])
{
  /* Proceed parameters */
  if (argc != 4)
  {
    g_printerr ("usage: %s <test_dir_uri> <number_of_iterations> <test_name>\n", argv[0]);
    return 1;
  }

  if (strcmp (argv[3], "query_info") == 0)
  {
    test_name = QUERYINFO;
  }
  else if (strcmp (argv[3], "enumeration") == 0)
  {
    test_name = ENUMERATION;
  }
  else if (strcmp (argv[3], "nautilus") == 0)
  {
    test_name = NAUTILUS;
  }
  else
  {
    g_printerr ("test name isn't one from \"query_info\" or \"enumeration\" or \"nautilus\".\n", argv[0]);
    return 1;
  }

  file = g_file_new_for_commandline_arg (argv[1]);
  i = iterations = atoi (argv[2]);
  first_values = g_array_sized_new (FALSE, FALSE, sizeof (gint64), iterations);
  second_values = g_array_sized_new (FALSE, FALSE, sizeof (gint64), iterations);

  /* Run tests */
  main_loop = g_main_loop_new (NULL, FALSE);
  g_file_mount_enclosing_volume (file, 0, NULL, NULL, mount_cb, NULL);
  g_main_loop_run (main_loop);

  g_array_free (first_values, TRUE);
  g_array_free (second_values, TRUE);
  g_object_unref (file);

  return 0;
}
