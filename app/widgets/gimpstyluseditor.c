#include "config.h"

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpwidgets/gimpwidgets.h"

#include "widgets-types.h"

#include "core/gimp.h"
#include "core/gimpcontext.h"

#include "gimpdataeditor.h"
#include "gimpdockable.h"
#include "gimpdocked.h"
#include "gimpuimanager.h"

#include "gimp-intl.h"

#include "gimpstyluseditor.h"

static void stylus_editor_constructed (GObject *object);
static void stylus_editor_dispose (GObject *object);
static void stylus_editor_slider_changed (GtkAdjustment *adjustment, StylusEditor *editor);

G_DEFINE_TYPE (StylusEditor, stylus_editor, GIMP_TYPE_EDITOR)

#define parent_class stylus_editor_parent_class

static void
stylus_editor_class_init (StylusEditorClass *klass)
{
  GObjectClass        *object_class = G_OBJECT_CLASS (klass);
  GimpDataEditorClass *editor_class = GIMP_DATA_EDITOR_CLASS (klass);

  object_class->constructed = stylus_editor_constructed;
  object_class->dispose     = stylus_editor_dispose;
  editor_class->title       = _ ("Stylus Editor");
}

static void
stylus_editor_init (StylusEditor *editor)
{
  /* Initialize slider components */
  editor->slider_adjustment = NULL;
  editor->slider_scale      = NULL;
}

static void
stylus_editor_dispose (GObject *object)
{
  StylusEditor *editor = STYLUS_EDITOR (object);

  if (editor->slider_adjustment)
    {
      g_object_unref (editor->slider_adjustment);
      editor->slider_adjustment = NULL;
    }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
stylus_editor_constructed (GObject *object)
{
  StylusEditor *editor = STYLUS_EDITOR (object);
  GtkWidget    *vbox;
  GtkWidget    *frame;
  GtkWidget    *scale;

  G_OBJECT_CLASS (parent_class)->constructed (object);

  // Create main container
  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_add (GTK_CONTAINER (editor), vbox);
  gtk_widget_show (vbox);

  frame = gimp_frame_new (_ ("Stylus Settings"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  editor->slider_adjustment = gtk_adjustment_new (50.0, 0.0, 100.0, 1.0, 10.0, 0.0);

  scale = gimp_spin_scale_new (editor->slider_adjustment, _ ("Pressure Sensitivity"), 1);
  gimp_spin_scale_set_constrain_drag (GIMP_SPIN_SCALE (scale), TRUE);

  gtk_container_add (GTK_CONTAINER (frame), scale);
  gtk_widget_show (scale);

  editor->slider_scale = scale;

  // Connect signal for slider changes
  g_signal_connect (editor->slider_adjustment, "value-changed",
                    G_CALLBACK (stylus_editor_slider_changed), editor);

  gimp_docked_set_show_button_bar (GIMP_DOCKED (object), FALSE);
}

static void
stylus_editor_slider_changed (GtkAdjustment *adjustment, StylusEditor *editor)
{
  // Function that happens when slider value is changed
  gdouble value = gtk_adjustment_get_value (adjustment);

  g_print ("Stylus slider value changed to: %.1f\n", value);
}

/* Public functions */

GtkWidget *
stylus_editor_new (GimpContext *context, GimpMenuFactory *menu_factory)
{
  g_return_val_if_fail (GIMP_IS_CONTEXT (context), NULL);

  GtkWidget *dock = g_object_new (STYLUS_TYPE_DOCK, "context", context,
                                  "menu-identifier", "<StylusEditor>", NULL);

  return dock;
}