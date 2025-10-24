#include "config.h"

#include <gegl.h>
#include <gtk/gtk.h>
#include <math.h>

#include "libgimpwidgets/gimpwidgets.h"

#include "widgets-types.h"

#include "core/gimp.h"
#include "core/gimpcontext.h"
#include "core/gimpcurve.h"

#include "libgimpconfig/gimpconfig.h"

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

/* Module-level storage for current power setting */
static gdouble current_power_setting = 1.0;

/* Module-level reference to the stylus editor instance (for per-brush curves) */
static StylusEditor *global_stylus_editor = NULL;

static void stylus_editor_constructed (GObject *object);
static void stylus_editor_dispose (GObject *object);
static void stylus_editor_slider_changed (GtkAdjustment *adjustment, StylusEditor *editor);
static void stylus_editor_preset_changed (GtkComboBox *combo, StylusEditor *editor);
static void stylus_editor_natural_curve_clicked (GtkButton *button, StylusEditor *editor);
static void stylus_editor_reset_all_curves_clicked (GtkButton *button, StylusEditor *editor);
static void stylus_editor_calibrate_clicked (GtkButton *button, StylusEditor *editor);
static gboolean stylus_editor_update_pressure (gpointer data);
static gboolean stylus_editor_curve_draw (GtkWidget *widget, cairo_t *cr, StylusEditor *editor);
static void stylus_editor_brush_changed (GimpContext *context, GimpBrush *brush, StylusEditor *editor);
static void stylus_editor_save_brush_curves (StylusEditor *editor);
static void stylus_editor_load_brush_curves (StylusEditor *editor);

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
  editor->reset_all_button = NULL;
  editor->pressure_label = NULL;
  editor->curve_view = NULL;
  editor->preset_combo = NULL;
  editor->context = NULL;
  editor->last_active_device = NULL;
  editor->curve_view_device = NULL;
  
  /* Initialize per-brush curve storage */
  editor->brush_curves = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, g_object_unref);
  editor->current_brush = NULL;
  editor->global_default_curve = NULL;
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

  if (editor->brush_curves)
    {
      g_hash_table_destroy (editor->brush_curves);
      editor->brush_curves = NULL;
    }

  if (editor->global_default_curve)
    {
      g_object_unref (editor->global_default_curve);
      editor->global_default_curve = NULL;
    }

  if (editor->context)
    {
      g_signal_handlers_disconnect_by_func (editor->context,
                                            stylus_editor_brush_changed,
                                            editor);
      g_object_unref (editor->context);
      editor->context = NULL;
    }

  /* Clear global reference if this is the global instance */
  if (global_stylus_editor == editor)
    global_stylus_editor = NULL;

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
  GtkWidget    *reset_button_box;

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

  /* Power slider: ranges from 0.5 to 6.0, default 1.0 (linear)
   * Step increment: 0.01 for fine control (0.50, 0.51, 0.52, ...)
   * Page increment: 0.1 for coarser adjustments
   * Constrain drag = FALSE for free sliding without snapping
   */
  editor->slider_adjustment = gtk_adjustment_new (1.0, 0.5, 6.0, 0.01, 0.1, 0.0);

  scale = gimp_spin_scale_new (editor->slider_adjustment, _ ("Power"), 2);
  gimp_spin_scale_set_constrain_drag (GIMP_SPIN_SCALE (scale), FALSE);

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

  /* Add Reset Curve buttons - two buttons side by side */
  reset_button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (box_in_frame), reset_button_box, FALSE, FALSE, 0);
  gtk_widget_show (reset_button_box);

  editor->natural_curve_button = gtk_button_new_with_label (_ ("Reset Curve"));
  gtk_box_pack_start (GTK_BOX (reset_button_box), editor->natural_curve_button, TRUE, TRUE, 0);
  gtk_widget_show (editor->natural_curve_button);
  g_signal_connect (editor->natural_curve_button, "clicked",
                    G_CALLBACK (stylus_editor_natural_curve_clicked), editor);

  editor->reset_all_button = gtk_button_new_with_label (_ ("Reset All Curves"));
  gtk_box_pack_start (GTK_BOX (reset_button_box), editor->reset_all_button, TRUE, TRUE, 0);
  gtk_widget_show (editor->reset_all_button);
  g_signal_connect (editor->reset_all_button, "clicked",
                    G_CALLBACK (stylus_editor_reset_all_curves_clicked), editor);

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
  
  /* Connect custom draw handler for centered white axis labels */
  g_signal_connect_after (editor->curve_view, "draw",
                          G_CALLBACK (stylus_editor_curve_draw), editor);
  
  gtk_box_pack_start (GTK_BOX (box_in_frame), editor->curve_view, FALSE, FALSE, 0);
  gtk_widget_show (editor->curve_view);

  /* Start timer to update device info */
  g_timeout_add (100, stylus_editor_update_pressure, editor);

  gimp_docked_set_show_button_bar (GIMP_DOCKED (object), FALSE);
}

static void
stylus_editor_slider_changed (GtkAdjustment *adjustment, StylusEditor *editor)
{
  /* Get power value from slider and store it */
  current_power_setting = gtk_adjustment_get_value (adjustment);
  
  /* Just display the current power setting - it will be applied during calibration */
  g_print ("Power setting: %.2f (will be applied on next calibration)\n", current_power_setting);
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
  const gchar *brush_name;
  
  /* Get device manager */
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
  
  device_info = gimp_device_manager_get_current_device (device_manager);
  if (!device_info)
    {
      g_print ("No current device available.\n");
      return;
    }
  
  /* Get the device's pressure curve */
  pressure_curve = gimp_device_info_get_curve (device_info, GDK_AXIS_PRESSURE);
  if (!pressure_curve)
    {
      g_print ("No pressure curve available.\n");
      return;
    }
  
  if (editor->current_brush)
    {
      brush_name = gimp_object_get_name (GIMP_OBJECT (editor->current_brush));
      g_print ("\n=== Resetting Curve for Brush '%s' ===\n", brush_name);
      
      /* Remove this brush's curve from storage */
      g_hash_table_remove (editor->brush_curves, brush_name);
      g_print ("  Removed stored curve for brush '%s'\n", brush_name);
    }
  else
    {
      g_print ("\n=== Resetting Pressure Curve to Linear (x1.0) ===\n");
    }
  
  /* Reset current device to linear 1:1 curve */
  g_print ("  Resetting device: %s\n", gimp_object_get_name (device_info));
  gimp_curve_reset (pressure_curve, FALSE);
  
  g_print ("Pressure curve reset to linear (x1.0)\n");
  g_print ("==========================================\n\n");
  
  /* Save brush curves and device configurations */
  stylus_editor_save_brush_curves (editor);
  gimp_devices_save (editor->context->gimp, TRUE);
  
  g_signal_emit (editor, stylus_editor_signals[NATURAL_CURVE_REQUESTED], 0);
}

/* ===============================================
 * RESET ALL CURVES BUTTON HANDLER
 * =============================================== */
static void
stylus_editor_reset_all_curves_clicked (GtkButton *button, StylusEditor *editor)
{
  GimpDeviceManager *device_manager;
  GimpDeviceInfo *device_info;
  GimpCurve *pressure_curve;
  
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
  
  g_print ("\n=== Resetting ALL Pressure Curves to Linear (x1.0) ===\n");
  
  /* Clear all per-brush stored curves */
  g_hash_table_remove_all (editor->brush_curves);
  g_print ("  Cleared all per-brush curves from storage\n");
  
  /* Clear global default curve */
  if (editor->global_default_curve)
    {
      g_object_unref (editor->global_default_curve);
      editor->global_default_curve = NULL;
      g_print ("  Cleared global default curve\n");
    }
  
  /* Reset curves for ALL devices */
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
        
        g_print ("  Resetting device: %s\n", gimp_object_get_name (device_info));
        
        /* Reset to linear 1:1 curve */
        gimp_curve_reset (pressure_curve, FALSE);
      }
  }
  
  g_print ("All pressure curves reset to linear (x1.0)\n");
  g_print ("All per-brush curves cleared\n");
  g_print ("==========================================\n\n");
  
  /* Save empty brush curves file and device configurations */
  stylus_editor_save_brush_curves (editor);
  gimp_devices_save (editor->context->gimp, TRUE);
  
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

/* ===============================================
 * CUSTOM CURVE DRAW HANDLER
 * Draws centered white axis labels on top of the curve view
 * =============================================== */
static gboolean
stylus_editor_curve_draw (GtkWidget *widget, cairo_t *cr, StylusEditor *editor)
{
  PangoLayout *layout;
  gint width, height;
  gint layout_width, layout_height;
  const gint border = 6;  /* Match the border used in gimpcurveview.c */
  
  /* Get widget dimensions */
  width = gtk_widget_get_allocated_width (widget);
  height = gtk_widget_get_allocated_height (widget);
  
  /* Create pango layout for text rendering */
  layout = gtk_widget_create_pango_layout (widget, NULL);
  
  /* Set white color for labels */
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  
  /* Draw X-axis label (centered horizontally at bottom) */
  pango_layout_set_text (layout, _ ("pen pressure"), -1);
  pango_layout_get_pixel_size (layout, &layout_width, &layout_height);
  
  cairo_move_to (cr,
                 border + (width / 2.0) - (layout_width / 2.0),
                 height - border - layout_height);
  pango_cairo_show_layout (cr, layout);
  
  /* Draw Y-axis label (centered vertically on left side, rotated) */
  pango_layout_set_text (layout, _ ("pressure"), -1);
  pango_layout_get_pixel_size (layout, &layout_width, &layout_height);
  
  cairo_save (cr);
  cairo_move_to (cr,
                 2 * border,
                 border + (height / 2.0) + (layout_width / 2.0));
  cairo_rotate (cr, - G_PI / 2);
  pango_cairo_show_layout (cr, layout);
  cairo_restore (cr);
  
  /* Clean up */
  g_object_unref (layout);
  
  return FALSE;  /* Allow other handlers to run */
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
      GimpCurve *stored_curve;
      const gchar *brush_name;
      
      editor->context = context;
      g_object_ref (editor->context);
      
      /* Set global reference for per-brush curve access */
      global_stylus_editor = editor;
      
      /* Connect to brush-changed signal for per-brush curves */
      g_signal_connect (editor->context, "brush-changed",
                        G_CALLBACK (stylus_editor_brush_changed),
                        editor);
      
      /* Store current brush */
      editor->current_brush = gimp_context_get_brush (context);
      
      /* Load saved brush curves from disk */
      stylus_editor_load_brush_curves (editor);
      
      /* Initialize curve view with current device's pressure curve */
      device_manager = gimp_devices_get_manager (context->gimp);
      if (device_manager)
        {
          device_info = gimp_device_manager_get_current_device (device_manager);
          if (device_info && editor->curve_view)
            {
              stored_curve = NULL;
              
              /* Check if current brush has a stored curve */
              if (editor->current_brush)
                {
                  brush_name = gimp_object_get_name (GIMP_OBJECT (editor->current_brush));
                  stored_curve = g_hash_table_lookup (editor->brush_curves, brush_name);
                  
                  if (stored_curve)
                    {
                      g_print ("Applying stored curve for initial brush '%s'\n", brush_name);
                      pressure_curve = gimp_device_info_get_curve (device_info, GDK_AXIS_PRESSURE);
                      if (pressure_curve)
                        {
                          gimp_config_copy (GIMP_CONFIG (stored_curve),
                                           GIMP_CONFIG (pressure_curve),
                                           GIMP_CONFIG_PARAM_SERIALIZE);
                        }
                    }
                }
              
              /* Get the curve again after potentially applying stored curve */
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

/* Brush changed handler - manages per-brush pressure curves */
static void
stylus_editor_brush_changed (GimpContext *context,
                             GimpBrush   *brush,
                             StylusEditor *editor)
{
  GimpDeviceManager *device_manager;
  GimpDeviceInfo *device_info;
  GimpCurve *stored_curve;
  const gchar *brush_name;
  
  if (!brush)
    return;
  
  brush_name = gimp_object_get_name (GIMP_OBJECT (brush));
  g_print ("\n=== Brush Changed to: %s ===\n", brush_name);
  g_print ("  Hash table size: %d\n", g_hash_table_size (editor->brush_curves));
  
  /* Get current device */
  device_manager = gimp_devices_get_manager (context->gimp);
  if (!device_manager)
    {
      g_print ("  ERROR: No device manager!\n");
      return;
    }
  
  device_info = gimp_device_manager_get_current_device (device_manager);
  if (!device_info)
    {
      g_print ("  ERROR: No device info!\n");
      return;
    }
  
  g_print ("  Device: %s\n", gimp_object_get_name (GIMP_OBJECT (device_info)));
  
  /* Check if we have a stored curve for this brush */
  stored_curve = g_hash_table_lookup (editor->brush_curves, brush_name);
  g_print ("  Stored curve lookup result: %p\n", (void*)stored_curve);
  
  if (stored_curve)
    {
      GimpCurve *device_curve;
      
      g_print ("  Found stored curve for brush '%s', applying it\n", brush_name);
      
      /* Copy stored curve to device */
      device_curve = gimp_device_info_get_curve (device_info, GDK_AXIS_PRESSURE);
      if (device_curve)
        {
          gimp_config_copy (GIMP_CONFIG (stored_curve),
                           GIMP_CONFIG (device_curve),
                           GIMP_CONFIG_PARAM_SERIALIZE);
          
          /* Update curve view */
          if (editor->curve_view)
            {
              gimp_curve_view_set_curve (GIMP_CURVE_VIEW (editor->curve_view),
                                        device_curve, NULL);
            }
        }
    }
  else
    {
      GimpCurve *device_curve;
      
      g_print ("  No per-brush curve for '%s'\n", brush_name);
      
      device_curve = gimp_device_info_get_curve (device_info, GDK_AXIS_PRESSURE);
      if (device_curve)
        {
          /* Check if there's a global default curve */
          if (editor->global_default_curve)
            {
              g_print ("  Applying global default curve\n");
              gimp_config_copy (GIMP_CONFIG (editor->global_default_curve),
                               GIMP_CONFIG (device_curve),
                               GIMP_CONFIG_PARAM_SERIALIZE);
            }
          else
            {
              g_print ("  No global default, resetting to linear curve\n");
              gimp_curve_reset (device_curve, FALSE);
            }
          
          /* Update curve view */
          if (editor->curve_view)
            {
              gimp_curve_view_set_curve (GIMP_CURVE_VIEW (editor->curve_view),
                                        device_curve, NULL);
            }
        }
    }
  
  /* Update current brush */
  editor->current_brush = brush;
  
  g_print ("============================\n\n");
}

/* Public function to get current power setting */
gdouble
stylus_editor_get_power (Gimp *gimp)
{
  return current_power_setting;
}

/* Store pressure curve for current or all brushes */
void
stylus_editor_store_curve (Gimp *gimp, GimpCurve *curve, gboolean apply_to_all)
{
  if (!global_stylus_editor)
    {
      g_print ("Warning: No stylus editor instance available for per-brush curves\n");
      return;
    }
  
  g_print ("\n=== stylus_editor_store_curve called ===\n");
  g_print ("  apply_to_all: %s\n", apply_to_all ? "YES" : "NO");
  g_print ("  Hash table size BEFORE: %d\n", g_hash_table_size (global_stylus_editor->brush_curves));
  
  if (apply_to_all)
    {
      g_print ("  Storing curve for ALL brushes (setting as global default)\n");
      
      /* Clear all stored per-brush curves */
      g_hash_table_remove_all (global_stylus_editor->brush_curves);
      
      /* Store as global default curve */
      if (global_stylus_editor->global_default_curve)
        g_object_unref (global_stylus_editor->global_default_curve);
      
      global_stylus_editor->global_default_curve = 
        GIMP_CURVE (gimp_config_duplicate (GIMP_CONFIG (curve)));
      
      g_print ("  Set global default curve\n");
    }
  else if (global_stylus_editor->current_brush)
    {
      const gchar *brush_name = gimp_object_get_name (GIMP_OBJECT (global_stylus_editor->current_brush));
      GimpCurve *curve_copy;
      
      g_print ("  Storing curve for brush '%s'\n", brush_name);
      
      /* Create a copy of the curve to store */
      curve_copy = GIMP_CURVE (gimp_config_duplicate (GIMP_CONFIG (curve)));
      
      /* Store it in the hash table (will replace any existing curve) */
      g_hash_table_insert (global_stylus_editor->brush_curves,
                          g_strdup (brush_name),
                          curve_copy);
      
      g_print ("  Successfully stored in hash table\n");
    }
  else
    {
      g_print ("  ERROR: No current brush set!\n");
    }
  
  g_print ("  Hash table size AFTER: %d\n", g_hash_table_size (global_stylus_editor->brush_curves));
  g_print ("======================================\n\n");
  
  /* Save brush curves to disk */
  stylus_editor_save_brush_curves (global_stylus_editor);
}

/* Get current brush name */
const gchar*
stylus_editor_get_current_brush_name (Gimp *gimp)
{
  if (!global_stylus_editor || !global_stylus_editor->current_brush)
    return NULL;
  
  return gimp_object_get_name (GIMP_OBJECT (global_stylus_editor->current_brush));
}

/* Save brush curves to disk */
static void
stylus_editor_save_brush_curves (StylusEditor *editor)
{
  GimpConfigWriter *writer;
  GFile *file;
  GList *keys;
  GList *iter;
  
  if (!editor || !editor->context)
    return;
  
  file = gimp_directory_file ("brushcurvesrc", NULL);
  
  g_print ("\n=== Saving brush curves ===\n");
  g_print ("Writing '%s'\n", gimp_file_get_utf8_name (file));
  
  writer = gimp_config_writer_new_from_file (file,
                                             TRUE,
                                             "GIMP brush curves\n\n"
                                             "This file stores per-brush pressure curves.",
                                             NULL);
  g_object_unref (file);
  
  if (!writer)
    return;
  
  /* Write each brush curve */
  keys = g_hash_table_get_keys (editor->brush_curves);
  
  for (iter = keys; iter; iter = iter->next)
    {
      const gchar *brush_name = iter->data;
      GimpCurve *curve = g_hash_table_lookup (editor->brush_curves, brush_name);
      
      g_print ("  Saving brush '%s' -> curve %p\n", brush_name, (void*)curve);
      
      if (curve)
        {
          gimp_config_writer_open (writer, "brush-curve");
          gimp_config_writer_string (writer, brush_name);
          
          /* Write curve as a nested object */
          gimp_config_writer_open (writer, "curve");
          gimp_config_serialize_properties (GIMP_CONFIG (curve), writer);
          gimp_config_writer_close (writer);
          
          gimp_config_writer_close (writer);
        }
    }
  
  g_list_free (keys);
  
  /* Write global default curve if it exists */
  if (editor->global_default_curve)
    {
      g_print ("  Saving global default curve -> %p\n", (void*)editor->global_default_curve);
      
      gimp_config_writer_open (writer, "global-default-curve");
      
      /* Write curve as a nested object */
      gimp_config_writer_open (writer, "curve");
      gimp_config_serialize_properties (GIMP_CONFIG (editor->global_default_curve), writer);
      gimp_config_writer_close (writer);
      
      gimp_config_writer_close (writer);
    }
  
  gimp_config_writer_finish (writer, "end of brush curves", NULL);
  
  g_print ("Saved %d brush curves to brushcurvesrc\n",
          g_hash_table_size (editor->brush_curves));
  if (editor->global_default_curve)
    g_print ("Saved global default curve\n");
  g_print ("===========================\n\n");
}

/* Load brush curves from disk */
static void
stylus_editor_load_brush_curves (StylusEditor *editor)
{
  GFile *file;
  GScanner *scanner;
  GTokenType token;
  GList *loaded_keys;
  GList *loaded_iter;
  
  if (!editor || !editor->context)
    return;
  
  file = gimp_directory_file ("brushcurvesrc", NULL);
  
  g_print ("\n=== Loading brush curves ===\n");
  g_print ("Parsing '%s'\n", gimp_file_get_utf8_name (file));
  
  scanner = gimp_scanner_new_file (file, NULL);
  g_object_unref (file);
  
  if (!scanner)
    {
      g_print ("No saved brush curves found (this is normal on first run)\n");
      return;
    }
  
  g_scanner_scope_add_symbol (scanner, 0, "brush-curve", GINT_TO_POINTER (0));
  g_scanner_scope_add_symbol (scanner, 0, "global-default-curve", GINT_TO_POINTER (2));
  g_scanner_scope_add_symbol (scanner, 0, "curve", GINT_TO_POINTER (1));
  
  /* Parse the file - look for (brush-curve ...) and (global-default-curve ...) entries */
  while (g_scanner_peek_next_token (scanner) == G_TOKEN_LEFT_PAREN)
    {
      g_print ("  ===== Starting new brush-curve iteration =====\n");
      
      token = g_scanner_get_next_token (scanner);
      g_print ("  Consumed LEFT_PAREN: %d\n", token);
      
      if (token != G_TOKEN_LEFT_PAREN)
        {
          g_print ("  ERROR: Expected LEFT_PAREN but got %d\n", token);
          break;
        }
      
      token = g_scanner_get_next_token (scanner);
      g_print ("  Next token (should be symbol): %d\n", token);
      
      if (token == G_TOKEN_SYMBOL &&
          scanner->value.v_symbol == GINT_TO_POINTER (0)) /* brush-curve */
        {
          gchar *brush_name = NULL;
          GimpCurve *curve;
          
          /* Read brush name */
          token = g_scanner_get_next_token (scanner);
          if (token == G_TOKEN_STRING)
            {
              brush_name = g_strdup (scanner->value.v_string);
              g_print ("  Found brush-curve for '%s'\n", brush_name);
            }
          else
            {
              g_print ("  ERROR: Expected string token, got %d\n", token);
            }
          
          if (brush_name)
            {
              /* Create a new curve */
              curve = GIMP_CURVE (gimp_curve_new ("brush curve"));
              
              /* Look for (curve ...) block */
              token = g_scanner_get_next_token (scanner);
              if (token == G_TOKEN_LEFT_PAREN)
                {
                  token = g_scanner_get_next_token (scanner);
                  if (token == G_TOKEN_SYMBOL &&
                      scanner->value.v_symbol == GINT_TO_POINTER (1)) /* curve */
                    {
                      g_print ("  Found curve data block\n");
                      
                      /* Deserialize curve properties */
                      if (gimp_config_deserialize_properties (GIMP_CONFIG (curve),
                                                             scanner,
                                                             1))
                        {
                          /* Store the loaded curve */
                          g_hash_table_insert (editor->brush_curves,
                                              brush_name,
                                              curve);
                          
                          g_print ("  Successfully loaded and stored curve for brush '%s' -> %p\n", 
                                  brush_name, (void*)curve);
                          
                          /* Consume closing paren of (curve ...) */
                          token = g_scanner_get_next_token (scanner);
                          g_print ("  After deserialize, consumed token: %d (expecting %d = RIGHT_PAREN)\n", 
                                  token, G_TOKEN_RIGHT_PAREN);
                          
                          /* Consume closing paren of (brush-curve ...) */
                          token = g_scanner_get_next_token (scanner);
                          g_print ("  Consumed closing paren of brush-curve: %d\n", token);
                        }
                      else
                        {
                          g_print ("  ERROR: Failed to deserialize curve properties for brush '%s'\n", brush_name);
                          g_free (brush_name);
                          g_object_unref (curve);
                        }
                    }
                  else
                    {
                      g_print ("  ERROR: Expected 'curve' symbol, got token %d\n", token);
                      g_free (brush_name);
                      g_object_unref (curve);
                    }
                }
              else
                {
                  g_print ("  ERROR: Expected left paren for curve block, got token %d\n", token);
                  g_free (brush_name);
                  g_object_unref (curve);
                }
            }
        }
      else if (token == G_TOKEN_SYMBOL &&
               scanner->value.v_symbol == GINT_TO_POINTER (2)) /* global-default-curve */
        {
          GimpCurve *curve;
          
          g_print ("  Found global-default-curve\n");
          
          /* Look for (curve ...) block */
          token = g_scanner_get_next_token (scanner);
          if (token == G_TOKEN_LEFT_PAREN)
            {
              token = g_scanner_get_next_token (scanner);
              if (token == G_TOKEN_SYMBOL &&
                  scanner->value.v_symbol == GINT_TO_POINTER (1)) /* curve */
                {
                  g_print ("  Found curve data block for global default\n");
                  
                  /* Create a new curve */
                  curve = GIMP_CURVE (gimp_curve_new ("global default curve"));
                  
                  /* Deserialize curve properties */
                  if (gimp_config_deserialize_properties (GIMP_CONFIG (curve),
                                                         scanner,
                                                         1))
                    {
                      /* Store as global default */
                      if (editor->global_default_curve)
                        g_object_unref (editor->global_default_curve);
                      
                      editor->global_default_curve = curve;
                      
                      g_print ("  Successfully loaded global default curve -> %p\n", 
                              (void*)curve);
                      
                      /* Consume closing parens */
                      token = g_scanner_get_next_token (scanner);
                      g_print ("  After deserialize, consumed token: %d\n", token);
                      
                      token = g_scanner_get_next_token (scanner);
                      g_print ("  Consumed closing paren of global-default-curve: %d\n", token);
                    }
                  else
                    {
                      g_print ("  ERROR: Failed to deserialize global default curve\n");
                      g_object_unref (curve);
                    }
                }
              else
                {
                  g_print ("  ERROR: Expected 'curve' symbol for global default\n");
                }
            }
          else
            {
              g_print ("  ERROR: Expected left paren for global default curve block\n");
            }
        }
      else
        {
          g_print ("  Unknown symbol in file, skipping...\n");
        }
      
      g_print ("  ===== End of iteration, peeking next token =====\n");
      token = g_scanner_peek_next_token (scanner);
      g_print ("  Next token = %d (G_TOKEN_LEFT_PAREN=%d, G_TOKEN_RIGHT_PAREN=%d, G_TOKEN_EOF=%d)\n", 
              token, G_TOKEN_LEFT_PAREN, G_TOKEN_RIGHT_PAREN, G_TOKEN_EOF);
    }
  
  g_print ("  Parse loop ended, next token = %d\n", g_scanner_peek_next_token (scanner));
  
  gimp_scanner_unref (scanner);
  
  g_print ("Loaded %d brush curves from brushcurvesrc\n",
          g_hash_table_size (editor->brush_curves));
  if (editor->global_default_curve)
    g_print ("Loaded global default curve -> %p\n", (void*)editor->global_default_curve);
  
  /* Print all loaded brushes for verification */
  if (g_hash_table_size (editor->brush_curves) > 0)
    {
      loaded_keys = g_hash_table_get_keys (editor->brush_curves);
      
      g_print ("Loaded brushes in hash table:\n");
      for (loaded_iter = loaded_keys; loaded_iter; loaded_iter = loaded_iter->next)
        {
          const gchar *name = loaded_iter->data;
          GimpCurve *curve = g_hash_table_lookup (editor->brush_curves, name);
          g_print ("  - '%s' -> %p\n", name, (void*)curve);
        }
      g_list_free (loaded_keys);
    }
  
  g_print ("===========================\n\n");
}