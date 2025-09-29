#include "config.h"
#include "testdock.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

GtkWidget *
testdock_new (GimpDialogFactory *factory,
              GimpContext       *context,
              GimpUIManager     *ui_manager,
              gint               view_size)
{
  GtkWidget *box;
  GtkWidget *label;

  g_message ("testdock_new: entering");

  /* Avoid unused parameter warnings */
  (void) factory;
  (void) context;
  (void) ui_manager;
  (void) view_size;

  g_message ("testdock_new: creating box");
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

  g_message ("testdock_new: creating label");
  label = gtk_label_new ("This is Test Dock");

  g_message ("testdock_new: packing label");
  gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);

  g_message ("testdock_new: showing all");
  gtk_widget_show_all (box);

  g_message ("testdock_new: returning box");
  return box;
}
