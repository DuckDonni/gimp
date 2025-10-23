#include "config.h"

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpwidgets/gimpwidgets.h"

#include "widgets-types.h"

#include "core/gimp.h"
#include "core/gimpcontext.h"
#include "core/gimpcurve.h"

#include "gimpdataeditor.h"
#include "gimpdockable.h"
#include "gimpdocked.h"
#include "gimpuimanager.h"

#include "gimpdevices.h"
#include "gimpdevicemanager.h"
#include "gimpdeviceinfo.h"
#include "gimpdeviceinfo-coords.h"
#include "gimpcurveview.h"

#include "gimp-intl.h"

#include "gimpstyluseditor.h"
#include "gimppressurecalibrationdialog.h"

static void stylus_editor_constructed (GObject *object);
static void stylus_editor_dispose (GObject *object);
static void stylus_editor_slider_changed (GtkAdjustment *adjustment, StylusEditor *editor);
static void stylus_editor_preset_changed (GtkComboBox *combo, StylusEditor *editor);
static void stylus_editor_natural_curve_clicked (GtkButton *button, StylusEditor *editor);
static void stylus_editor_calibrate_clicked (GtkButton *button, StylusEditor *editor);
static gboolean stylus_editor_update_pressure (gpointer data);

G_DEFINE_TYPE (StylusEditor, stylus_editor, GIMP_TYPE_EDITOR)

#define parent_class stylus_editor_parent_class

enum
{
  NATURAL_CURVE_REQUESTED,
  LAST_SIGNAL
};

static guint stylus_editor_signals[LAST_SIGNAL] = { 0 };

static void
stylus_editor_class_init (StylusEditorClass *klass)
{
  GObjectClass        *object_class = G_OBJECT_CLASS (klass);
  GimpDataEditorClass *editor_class = GIMP_DATA_EDITOR_CLASS (klass);

  object_class->constructed = stylus_editor_constructed;
  object_class->dispose     = stylus_editor_dispose;
  editor_class->title       = _ ("Stylus Editor");

  stylus_editor_signals[NATURAL_CURVE_REQUESTED] =
    g_signal_new ("natural-curve-requested",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (StylusEditorClass, natural_curve_requested),
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  0);
}

static void
stylus_editor_init (StylusEditor *editor)
{
  /* Initialize slider components */
  editor->slider_adjustment = NULL;
  editor->slider_scale      = NULL;
  editor->natural_curve_button = NULL;
  editor->pressure_label = NULL;
  editor->curve_view = NULL;
  editor->preset_combo = NULL;
  editor->context = NULL;
  editor->last_active_device = NULL;
  editor->curve_view_device = NULL;
  editor->natural_curve_enabled = FALSE;
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

  if (editor->context)
    {
      g_object_unref (editor->context);
      editor->context = NULL;
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
  GtkWidget    *box_in_frame;

  G_OBJECT_CLASS (parent_class)->constructed (object);

  // Create main container
  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_add (GTK_CONTAINER (editor), vbox);
  gtk_widget_show (vbox);

  frame = gimp_frame_new (_ ("Stylus Settings"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  /* Container inside frame to stack slider and button */
  box_in_frame = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6); //the second argument is the spacing vertically in pixels
  gtk_container_add (GTK_CONTAINER (frame), box_in_frame);
  gtk_widget_show (box_in_frame);

  editor->slider_adjustment = gtk_adjustment_new (50.0, 0.0, 100.0, 1.0, 10.0, 0.0);

  scale = gimp_spin_scale_new (editor->slider_adjustment, _ ("Pressure Sensitivity"), 1);
  gimp_spin_scale_set_constrain_drag (GIMP_SPIN_SCALE (scale), TRUE);

  gtk_box_pack_start (GTK_BOX (box_in_frame), scale, FALSE, FALSE, 0);
  gtk_widget_show (scale);

  editor->slider_scale = scale;
  

  // Connect signal for slider changes
  g_signal_connect (editor->slider_adjustment, "value-changed",
                    G_CALLBACK (stylus_editor_slider_changed), editor);

  /* Add pressure display label */
  editor->pressure_label = gtk_label_new (_ ("Device: (detecting...)"));
  gtk_box_pack_start (GTK_BOX (box_in_frame), editor->pressure_label, FALSE, FALSE, 0);
  gtk_widget_show (editor->pressure_label);

  /* Add Preset selector dropdown (placeholder for future preset system) */
  editor->preset_combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (editor->preset_combo), _("Default"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (editor->preset_combo), _("Light Touch"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (editor->preset_combo), _("Heavy Pressure"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (editor->preset_combo), _("Sketching"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (editor->preset_combo), _("Inking"));
  gtk_combo_box_set_active (GTK_COMBO_BOX (editor->preset_combo), 0);
  gtk_box_pack_start (GTK_BOX (box_in_frame), editor->preset_combo, FALSE, FALSE, 0);
  gtk_widget_show (editor->preset_combo);
  
  /* Connect signal for preset changes (placeholder - does nothing yet) */
  g_signal_connect (editor->preset_combo, "changed",
                   G_CALLBACK (stylus_editor_preset_changed), editor);

  /* Add Natural Curve button under the slider */
  editor->natural_curve_button = gtk_button_new_with_label (_ ("Natural Curve: OFF (x1.0)"));
  gtk_box_pack_start (GTK_BOX (box_in_frame), editor->natural_curve_button, FALSE, FALSE, 0);
  gtk_widget_show (editor->natural_curve_button);

  g_signal_connect (editor->natural_curve_button, "clicked",
                    G_CALLBACK (stylus_editor_natural_curve_clicked), editor);

  /* Add Calibrate Pressure button */
  editor->calibrate_button = gtk_button_new_with_label (_ ("Calibrate Pressure..."));
  gtk_box_pack_start (GTK_BOX (box_in_frame), editor->calibrate_button, FALSE, FALSE, 0);
  gtk_widget_show (editor->calibrate_button);

  g_signal_connect (editor->calibrate_button, "clicked",
                    G_CALLBACK (stylus_editor_calibrate_clicked), editor);

  /* Add Pressure Curve view - read-only display (no user editing allowed) */
  editor->curve_view = gimp_curve_view_new ();
  gtk_widget_set_size_request (editor->curve_view, 200, 200);
  gtk_widget_set_sensitive (editor->curve_view, FALSE);  /* Disable all user interaction */
  gtk_box_pack_start (GTK_BOX (box_in_frame), editor->curve_view, FALSE, FALSE, 0);
  gtk_widget_show (editor->curve_view);

  /* Start timer to update device info */
  g_timeout_add (100, stylus_editor_update_pressure, editor);

  gimp_docked_set_show_button_bar (GIMP_DOCKED (object), FALSE);
}

static void
stylus_editor_slider_changed (GtkAdjustment *adjustment, StylusEditor *editor)
{
  /* Slider value changed - can be used for future features */
  gdouble value = gtk_adjustment_get_value (adjustment);
  
  /* Currently unused - placeholder for future pressure curve adjustments */
  (void) value;
}

static void
stylus_editor_preset_changed (GtkComboBox *combo, StylusEditor *editor)
{
  gchar *preset_name = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (combo));
  
  g_print ("Preset changed to: %s (placeholder - does nothing yet)\n", 
          preset_name ? preset_name : "(none)");
  
  /* Future: Load preset curve data and apply it to devices */
  
  g_free (preset_name);
}

static void
stylus_editor_natural_curve_clicked (GtkButton *button, StylusEditor *editor)
{
  GimpDeviceManager *device_manager;
  GimpDeviceInfo *device_info;
  GimpCurve *pressure_curve;
  gint i;
  
  /* Toggle the natural curve state */
  editor->natural_curve_enabled = !editor->natural_curve_enabled;
  
  /* Update button label */
  if (editor->natural_curve_enabled)
    {
      gtk_button_set_label (GTK_BUTTON (editor->natural_curve_button), 
                           _ ("Natural Curve: ON (x0.1)"));
    }
  else
    {
      gtk_button_set_label (GTK_BUTTON (editor->natural_curve_button), 
                           _ ("Natural Curve: OFF (x1.0)"));
    }
  
  /* Get device manager to iterate through all devices */
  if (!editor->context)
    {
      g_print ("No context available.\n");
      return;
    }
  
  device_manager = gimp_devices_get_manager (editor->context->gimp);
  if (!device_manager)
    {
      g_print ("No device manager available.\n");
      return;
    }
  
  g_print ("\n=== Applying Natural Curve to ALL devices ===\n");
  
  /* Apply curve to ALL devices */
  {
    GimpContainer *container = GIMP_CONTAINER (device_manager);
    GList *list;
    
    for (list = GIMP_LIST (container)->queue->head; list; list = g_list_next (list))
      {
        device_info = GIMP_DEVICE_INFO (list->data);
        
        /* Get the device's pressure curve */
        pressure_curve = gimp_device_info_get_curve (device_info, GDK_AXIS_PRESSURE);
        if (!pressure_curve)
          {
            g_print ("  Skipping '%s' (no pressure curve)\n", gimp_object_get_name (device_info));
            continue;
          }
        
        g_print ("  Applying to device: %s\n", gimp_object_get_name (device_info));
        
        /* Modify the pressure curve */
        if (editor->natural_curve_enabled)
          {
            /* Set curve to FREE mode so we can set individual samples */
            gimp_curve_set_curve_type (pressure_curve, GIMP_CURVE_FREE);
            
            /* Map all values: output = input * 0.1 */
            for (i = 0; i < 256; i++)
              {
                gdouble x = i / 255.0;
                gdouble y = x * 0.1;  /* Multiply by 0.1 */
                gimp_curve_set_curve (pressure_curve, x, y);
              }
          }
        else
          {
            /* Reset to linear 1:1 curve */
            gimp_curve_reset (pressure_curve, FALSE);
          }
      }
  }
  
  if (editor->natural_curve_enabled)
    g_print ("Natural Curve ENABLED (x0.1) on all devices\n");
  else
    g_print ("Natural Curve DISABLED (x1.0) on all devices\n");
  
  g_print ("==========================================\n\n");
  
  g_signal_emit (editor, stylus_editor_signals[NATURAL_CURVE_REQUESTED], 0);
}

/* ===============================================
 * CALIBRATE BUTTON HANDLER
 * =============================================== */
static void
stylus_editor_calibrate_clicked (GtkButton *button, StylusEditor *editor)
{
  GtkWidget *dialog;
  GtkWidget *toplevel;

  if (!editor->context)
    return;

  /* Get parent window */
  toplevel = gtk_widget_get_toplevel (GTK_WIDGET (editor));
  if (!GTK_IS_WINDOW (toplevel))
    toplevel = NULL;

  /* Create and show calibration dialog */
  dialog = gimp_pressure_calibration_dialog_new (editor->context, toplevel);
  
  g_signal_connect (dialog, "response",
                   G_CALLBACK (gtk_widget_destroy), NULL);

  gtk_widget_show (dialog);
  gtk_dialog_run (GTK_DIALOG (dialog));
}

/* ===============================================
 * PRESSURE UPDATE TIMER
 * =============================================== */
static gboolean
stylus_editor_update_pressure (gpointer data)
{
  StylusEditor *editor = STYLUS_EDITOR (data);
  GimpDeviceManager *device_manager;
  GimpDeviceInfo *device_info;
  GimpCoords coords;
  gchar *text;
  GdkWindow *window;

  /* Use the stored context */
  if (!editor->context)
    return TRUE; /* Continue timer */

  /* Get device manager and current device */
  device_manager = gimp_devices_get_manager (editor->context->gimp);
  if (!device_manager)
    return TRUE;

  device_info = gimp_device_manager_get_current_device (device_manager);
  if (!device_info)
    return TRUE;

  /* Track the last active device for pressure display only */
  editor->last_active_device = device_info;

  /* Try to get a window from the editor widget itself */
  window = gtk_widget_get_window (GTK_WIDGET (editor));
  
  if (window)
    {
      /* Get current device coordinates with a valid window */
      gimp_device_info_get_device_coords (device_info, window, &coords);
      
      /* Update the pressure label - shows device name and current pressure */
      text = g_strdup_printf (_ ("%s - Pressure: %.3f"), 
                             gimp_object_get_name (GIMP_OBJECT (device_info)),
                             coords.pressure);
      gtk_label_set_text (GTK_LABEL (editor->pressure_label), text);
      g_free (text);
    }
  else
    {
      /* No window yet, just show device name */
      text = g_strdup_printf (_ ("Device: %s"), 
                             gimp_object_get_name (GIMP_OBJECT (device_info)));
      gtk_label_set_text (GTK_LABEL (editor->pressure_label), text);
      g_free (text);
    }

  /* Continue the timer */
  return TRUE;
}

/* Public functions */

GtkWidget *
stylus_editor_new (GimpContext *context, GimpMenuFactory *menu_factory)
{
  GtkWidget *dock;

  g_return_val_if_fail (GIMP_IS_CONTEXT (context), NULL);

  dock = g_object_new (STYLUS_TYPE_DOCK, "context", context,
                      "menu-identifier", "<StylusEditor>", NULL);

  /* Store the context in the editor */
  if (STYLUS_EDITOR (dock))
    {
      StylusEditor *editor = STYLUS_EDITOR (dock);
      GimpDeviceManager *device_manager;
      GimpDeviceInfo *device_info;
      GimpCurve *pressure_curve;
      
      editor->context = context;
      g_object_ref (editor->context);
      
      /* Initialize curve view with current device's pressure curve */
      device_manager = gimp_devices_get_manager (context->gimp);
      if (device_manager)
        {
          device_info = gimp_device_manager_get_current_device (device_manager);
          if (device_info && editor->curve_view)
            {
              pressure_curve = gimp_device_info_get_curve (device_info, GDK_AXIS_PRESSURE);
              if (pressure_curve)
                {
                  gimp_curve_view_set_curve (GIMP_CURVE_VIEW (editor->curve_view), 
                                            pressure_curve, NULL);
                  g_print ("Curve view set to device: %s\n",
                          gimp_object_get_name (GIMP_OBJECT (device_info)));
                }
            }
        }
    }

  return dock;
}