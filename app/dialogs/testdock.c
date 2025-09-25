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


  /* Avoid unused parameter warnings */
  (void) factory;
  (void) context;
  (void) ui_manager;
  (void) view_size;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  label = gtk_label_new (_("This is Test Dock"));

  gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
  gtk_widget_show_all (box);


  return box;
}
