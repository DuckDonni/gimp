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

static void   test_dock_constructed (GObject *object);

G_DEFINE_TYPE (TestDock, test_dock, GIMP_TYPE_EDITOR)

#define parent_class test_dock_parent_class

static void
test_dock_class_init (TestDockClass *klass)
{
  GObjectClass        *object_class = G_OBJECT_CLASS (klass);
  GimpDataEditorClass *editor_class = GIMP_DATA_EDITOR_CLASS (klass);

  object_class->constructed = test_dock_constructed;
  editor_class->title       = _("Tests Dock");
}

static void
test_dock_init (TestDock *dock)
{
  // BLANK (IE NO UI)
}

static void
test_dock_constructed (GObject *object)
{
  G_OBJECT_CLASS (parent_class)->constructed (object);

  /* Optional: hide the button bar to keep it visually blank */
  gimp_docked_set_show_button_bar (GIMP_DOCKED (object), FALSE);
}

/* Public functions */

GtkWidget *
test_dock_new (GimpContext *context,
               GimpMenuFactory *menu_factory)
{
  g_return_val_if_fail (GIMP_IS_CONTEXT (context), NULL);

  GtkWidget *dock = g_object_new (TEST_TYPE_DOCK,
    
                                  "context",         context,
                                  
                                  "menu-identifier", "<TestDock>",
                                  NULL);

  return dock;
}