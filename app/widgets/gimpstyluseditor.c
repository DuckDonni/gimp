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

#include "gimpstyluseditor.h"

static void   stylus_editor_constructed (GObject *object);

G_DEFINE_TYPE (StylusEditor, stylus_editor, GIMP_TYPE_EDITOR)

#define parent_class stylus_editor_parent_class

static void
stylus_editor_class_init (StylusEditorClass *klass)
{
  GObjectClass        *object_class = G_OBJECT_CLASS (klass);
  GimpDataEditorClass *editor_class = GIMP_DATA_EDITOR_CLASS (klass);

  object_class->constructed = stylus_editor_constructed;
  editor_class->title       = _("Stylus Editor");
}

static void
stylus_editor_init (StylusEditor *dock)
{
  // BLANK (IE NO UI)
}

static void
stylus_editor_constructed (GObject *object)
{
  G_OBJECT_CLASS (parent_class)->constructed (object);

  /* Optional: hide the button bar to keep it visually blank */
  gimp_docked_set_show_button_bar (GIMP_DOCKED (object), FALSE);
}

/* Public functions */

GtkWidget *
stylus_editor_new (GimpContext *context,
               GimpMenuFactory *menu_factory)
{
  g_return_val_if_fail (GIMP_IS_CONTEXT (context), NULL);

  GtkWidget *dock = g_object_new (STYLUS_TYPE_DOCK,
    
                                  "context",         context,
                                  
                                  "menu-identifier", "<StylusEditor>",
                                  NULL);

  return dock;
}