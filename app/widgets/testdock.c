#include "config.h"

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpwidgets/gimpwidgets.h"

#include "widgets-types.h"

#include "core/gimp.h"
#include "core/gimpcontext.h"

#include "gimpdocked.h"
#include "gimpdockable.h"
#include "gimpdataeditor.h"
#include "gimpuimanager.h"

#include "gimp-intl.h"

#include "testdock.h"

/* Local function prototypes */
static void   test_dock_docked_iface_init (GimpDockedInterface *face);
static void   test_dock_constructed       (GObject            *object);
static void   test_dock_set_data          (GimpDataEditor     *editor,
                                          GimpData           *data);
static void   test_dock_set_context       (GimpDocked         *docked,
                                          GimpContext        *context);

G_DEFINE_TYPE_WITH_CODE (TestDock, test_dock, GIMP_TYPE_DATA_EDITOR,
                         G_IMPLEMENT_INTERFACE (GIMP_TYPE_DOCKED,
                                                test_dock_docked_iface_init))

#define parent_class test_dock_parent_class

static GimpDockedInterface *parent_docked_iface = NULL;

static void
test_dock_class_init (TestDockClass *klass)
{
  GObjectClass        *object_class = G_OBJECT_CLASS (klass);
  GimpDataEditorClass *editor_class = GIMP_DATA_EDITOR_CLASS (klass);

  object_class->constructed = test_dock_constructed;
  editor_class->set_data    = test_dock_set_data;
  editor_class->title       = _("Test Dock");  /* This sets the dock title */
}

static void
test_dock_docked_iface_init (GimpDockedInterface *iface)
{
  parent_docked_iface = g_type_interface_peek_parent (iface);

  if (! parent_docked_iface)
    parent_docked_iface = g_type_default_interface_peek (GIMP_TYPE_DOCKED);

  iface->set_context = test_dock_set_context;
}

static void
test_dock_init (TestDock *dock)
{
  /* This function is now empty - creating a completely blank dock */
  /* All UI elements (main_box, label, button) have been removed */
}

static void
test_dock_constructed (GObject *object)
{
  G_OBJECT_CLASS (parent_class)->constructed (object);

  /* Hide the button bar like the brush editor does */
  gimp_docked_set_show_button_bar (GIMP_DOCKED (object), FALSE);
}

static void
test_dock_set_data (GimpDataEditor *editor,
                    GimpData       *data)
{
  /* Chain up to parent implementation */
  GIMP_DATA_EDITOR_CLASS (parent_class)->set_data (editor, data);

  /* You can handle data-specific logic here if needed */
}

static void
test_dock_set_context (GimpDocked  *docked,
                       GimpContext *context)
{
  /* Chain up to parent implementation */
  parent_docked_iface->set_context (docked, context);

  /* Handle any context-specific updates here */
}

/* Public functions */

GtkWidget *
test_dock_new (GimpContext     *context,
               GimpMenuFactory *menu_factory)
{
  g_return_val_if_fail (GIMP_IS_CONTEXT (context), NULL);

  GtkWidget *dock = g_object_new (TEST_TYPE_DOCK,
                                  "menu-factory",    menu_factory,
                                  "menu-identifier", "<TestDock>",
                                  "ui-path",         "/test-dock-popup",
                                  "context",         context,
                                  NULL);

  g_printerr("[test_dock_new] created %p type=%s\n",
             dock, dock ? G_OBJECT_TYPE_NAME (dock) : "NULL");

  return dock;
}