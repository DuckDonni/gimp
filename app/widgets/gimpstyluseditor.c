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

/* Global toggle for enabling/disabling custom curves */
static gboolean custom_curves_enabled = TRUE;

static void      stylus_editor_constructed               (GObject     *object);
static void      stylus_editor_dispose                   (GObject     *object);
static void      stylus_editor_slider_changed            (GtkAdjustment *adjustment,
                                                          StylusEditor  *editor);
static void      stylus_editor_preset_changed            (GtkComboBox *combo,
                                                          StylusEditor *editor);
static void      stylus_editor_natural_curve_clicked     (GtkButton   *button,
                                                          StylusEditor *editor);
static void      stylus_editor_reset_all_curves_clicked  (GtkButton   *button,
                                                          StylusEditor *editor);
static void      stylus_editor_calibrate_clicked         (GtkButton   *button,
                                                          StylusEditor *editor);
static void      stylus_editor_toggle_curve_clicked      (GtkButton   *button,
                                                          StylusEditor *editor);
static gboolean  stylus_editor_update_pressure           (gpointer     data);
static void      stylus_editor_brush_changed             (GimpContext *context,
                                                          GimpBrush   *brush,
                                                          StylusEditor *editor);
static void      stylus_editor_curve_dirty               (GimpCurve   *curve,
                                                          StylusEditor *editor);
static void      stylus_editor_save_brush_curves         (StylusEditor *editor);
static void      stylus_editor_load_brush_curves         (StylusEditor *editor);
static gboolean  stylus_editor_block_events              (GtkWidget   *widget,
                                                          GdkEvent    *event,
                                                          gpointer     user_data);
static gboolean  stylus_editor_curve_draw                (GtkWidget   *widget,
                                                          cairo_t     *cr,
                                                          StylusEditor *editor);

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

  object_class->constructed = stylus_editor_constructed;
  object_class->dispose     = stylus_editor_dispose;

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
  editor->slider_adjustment = NULL;
  editor->slider_scale      = NULL;
  editor->natural_curve_button = NULL;
  editor->reset_all_button = NULL;
  editor->pressure_label = NULL;
  editor->curve_view = NULL;
  editor->preset_combo = NULL;
  editor->toggle_curve_button = NULL;
  editor->curve_state_label = NULL;
  editor->context = NULL;
  editor->last_active_device = NULL;
  editor->curve_view_device = NULL;

  editor->brush_curves = g_hash_table_new_full (g_str_hash,
                                                 g_str_equal,
                                                 g_free,
                                                 g_object_unref);
  editor->current_brush = NULL;
  editor->global_default_curve = NULL;
  editor->display_curve = NULL;
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

  if (editor->display_curve)
    {
      g_object_unref (editor->display_curve);
      editor->display_curve = NULL;
    }

  if (editor->curve_view_device)
    {
      GimpCurve *curve;

      curve = gimp_device_info_get_curve (editor->curve_view_device,
                                          GDK_AXIS_PRESSURE);
      if (curve)
        {
          g_signal_handlers_disconnect_by_func (curve,
                                                stylus_editor_curve_dirty,
                                                editor);
        }
      editor->curve_view_device = NULL;
    }

  if (editor->context)
    {
      g_signal_handlers_disconnect_by_func (editor->context,
                                            stylus_editor_brush_changed,
                                            editor);
      g_object_unref (editor->context);
      editor->context = NULL;
    }

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

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_add (GTK_CONTAINER (editor), vbox);
  gtk_widget_show (vbox);

  frame = gimp_frame_new (_ ("Stylus Settings"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  box_in_frame = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_add (GTK_CONTAINER (frame), box_in_frame);
  gtk_widget_show (box_in_frame);

  /* Power slider: ranges from 0.5 to 6.0, default 1.0 (linear)
   * Step increment: 0.01 for fine control (0.50, 0.51, 0.52, ...)
   * Page increment: 0.1 for coarser adjustments
   * Constrain drag = FALSE for free sliding without snapping
   */
  editor->slider_adjustment = gtk_adjustment_new (1.0, 0.5, 6.0,
                                                  0.01, 0.1, 0.0);

  scale = gimp_spin_scale_new (editor->slider_adjustment, _ ("Power"), 2);
  gimp_spin_scale_set_constrain_drag (GIMP_SPIN_SCALE (scale), FALSE);

  gtk_box_pack_start (GTK_BOX (box_in_frame), scale, FALSE, FALSE, 0);
  gtk_widget_show (scale);

  editor->slider_scale = scale;

  g_signal_connect (editor->slider_adjustment, "value-changed",
                    G_CALLBACK (stylus_editor_slider_changed), editor);

  editor->pressure_label = gtk_label_new (_ ("Device: (detecting...)"));
  gtk_box_pack_start (GTK_BOX (box_in_frame), editor->pressure_label, FALSE, FALSE, 0);
  gtk_widget_show (editor->pressure_label);

  editor->preset_combo = gtk_combo_box_text_new ();
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (editor->preset_combo),
                                   _("Default"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (editor->preset_combo),
                                   _("Light Touch"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (editor->preset_combo),
                                   _("Heavy Pressure"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (editor->preset_combo),
                                   _("Sketching"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (editor->preset_combo),
                                   _("Inking"));
  gtk_combo_box_set_active (GTK_COMBO_BOX (editor->preset_combo), 0);
  gtk_box_pack_start (GTK_BOX (box_in_frame), editor->preset_combo, FALSE, FALSE, 0);
  gtk_widget_show (editor->preset_combo);

  g_signal_connect (editor->preset_combo, "changed",
                    G_CALLBACK (stylus_editor_preset_changed), editor);

  /* Add toggle for custom curves */
  editor->curve_state_label = gtk_label_new (_("Custom Curves: Enabled"));
  gtk_box_pack_start (GTK_BOX (box_in_frame), editor->curve_state_label,
                      FALSE, FALSE, 0);
  gtk_widget_show (editor->curve_state_label);

  editor->toggle_curve_button = gtk_button_new_with_label (_("Toggle Custom Curve"));
  gtk_box_pack_start (GTK_BOX (box_in_frame), editor->toggle_curve_button,
                      FALSE, FALSE, 0);
  gtk_widget_show (editor->toggle_curve_button);
  g_signal_connect (editor->toggle_curve_button, "clicked",
                    G_CALLBACK (stylus_editor_toggle_curve_clicked), editor);

  reset_button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (box_in_frame), reset_button_box, FALSE, FALSE, 0);
  gtk_widget_show (reset_button_box);

  editor->natural_curve_button = gtk_button_new_with_label (_("Reset Curve"));
  gtk_box_pack_start (GTK_BOX (reset_button_box),
                      editor->natural_curve_button, TRUE, TRUE, 0);
  gtk_widget_show (editor->natural_curve_button);
  g_signal_connect (editor->natural_curve_button, "clicked",
                    G_CALLBACK (stylus_editor_natural_curve_clicked), editor);

  editor->reset_all_button = gtk_button_new_with_label (_("Reset All Curves"));
  gtk_box_pack_start (GTK_BOX (reset_button_box), editor->reset_all_button,
                      TRUE, TRUE, 0);
  gtk_widget_show (editor->reset_all_button);
  g_signal_connect (editor->reset_all_button, "clicked",
                    G_CALLBACK (stylus_editor_reset_all_curves_clicked), editor);

  editor->calibrate_button =
    gtk_button_new_with_label (_("Calibrate Pressure..."));
  
  gtk_box_pack_start (GTK_BOX (box_in_frame), editor->calibrate_button,
                      FALSE, FALSE, 0);
  gtk_widget_show (editor->calibrate_button);

  g_signal_connect (editor->calibrate_button, "clicked",
                    G_CALLBACK (stylus_editor_calibrate_clicked), editor);

  editor->curve_view = gimp_curve_view_new ();
  gtk_widget_set_size_request (editor->curve_view, 200, 200);
  gtk_widget_set_sensitive (editor->curve_view, TRUE);

  gtk_widget_add_events (editor->curve_view,
                         GDK_BUTTON_PRESS_MASK |
                         GDK_BUTTON_RELEASE_MASK |
                         GDK_POINTER_MOTION_MASK |
                         GDK_SCROLL_MASK);
  g_signal_connect (editor->curve_view, "button-press-event",
                    G_CALLBACK (stylus_editor_block_events), NULL);
  g_signal_connect (editor->curve_view, "button-release-event",
                    G_CALLBACK (stylus_editor_block_events), NULL);
  g_signal_connect (editor->curve_view, "motion-notify-event",
                    G_CALLBACK (stylus_editor_block_events), NULL);
  g_signal_connect (editor->curve_view, "scroll-event",
                    G_CALLBACK (stylus_editor_block_events), NULL);
  
  /* Connect custom draw handler for centered white axis labels */
  g_signal_connect_after (editor->curve_view, "draw",
                          G_CALLBACK (stylus_editor_curve_draw), editor);
  
  gtk_box_pack_start (GTK_BOX (box_in_frame), editor->curve_view,
                      FALSE, FALSE, 0);
  gtk_widget_show (editor->curve_view);

  g_timeout_add (100, stylus_editor_update_pressure, editor);

  gimp_docked_set_show_button_bar (GIMP_DOCKED (object), FALSE);
}

static void
stylus_editor_slider_changed (GtkAdjustment *adjustment,
                               StylusEditor  *editor)
{
  current_power_setting = gtk_adjustment_get_value (adjustment);

  g_print ("Power setting: %.2f (will be applied on next calibration)\n",
           current_power_setting);
}

static void
stylus_editor_preset_changed (GtkComboBox  *combo,
                               StylusEditor *editor)
{
  gchar *preset_name;

  preset_name = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (combo));

  g_print ("Preset changed to: %s (placeholder - does nothing yet)\n",
          preset_name ? preset_name : "(none)");

  g_free (preset_name);
}

static void
stylus_editor_natural_curve_clicked (GtkButton    *button,
                                      StylusEditor *editor)
{
  GimpDeviceManager *device_manager;
  GimpDeviceInfo    *device_info;
  GimpCurve         *pressure_curve;
  const gchar       *brush_name;

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

  pressure_curve = gimp_device_info_get_curve (device_info, GDK_AXIS_PRESSURE);
  if (!pressure_curve)
    {
      g_print ("No pressure curve available.\n");
      return;
    }
  
  if (editor->current_brush)
    {
      brush_name = gimp_object_get_name (GIMP_OBJECT (editor->current_brush));
      g_print ("\n=== Resetting Curve for Brush '%s' ===\n",
               brush_name);

      g_hash_table_remove (editor->brush_curves, brush_name);
      g_print ("  Removed stored curve for brush '%s'\n", brush_name);
    }
  else
    {
      g_print ("\n=== Resetting Pressure Curve to Linear (x1.0) ===\n");
    }

  g_print ("  Resetting device: %s\n",
           gimp_object_get_name (device_info));
  gimp_curve_reset (pressure_curve, FALSE);
  
  g_print ("Pressure curve reset to linear (x1.0)\n");
  g_print ("==========================================\n\n");

  stylus_editor_save_brush_curves (editor);
  gimp_devices_save (editor->context->gimp, TRUE);
  
  g_signal_emit (editor, stylus_editor_signals[NATURAL_CURVE_REQUESTED], 0);
}

static void
stylus_editor_reset_all_curves_clicked (GtkButton    *button,
                                         StylusEditor *editor)
{
  GimpDeviceManager *device_manager;
  GimpDeviceInfo    *device_info;
  GimpCurve         *pressure_curve;

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

  g_hash_table_remove_all (editor->brush_curves);
  g_print ("  Cleared all per-brush curves from storage\n");

  if (editor->global_default_curve)
    {
      g_object_unref (editor->global_default_curve);
      editor->global_default_curve = NULL;
      g_print ("  Cleared global default curve\n");
    }

  {
    GimpContainer *container = GIMP_CONTAINER (device_manager);
    GList         *list;

    for (list = GIMP_LIST (container)->queue->head; list;
         list = g_list_next (list))
      {
        device_info = GIMP_DEVICE_INFO (list->data);

        pressure_curve = gimp_device_info_get_curve (device_info,
                                                     GDK_AXIS_PRESSURE);
        if (!pressure_curve)
          {
            g_print ("  Skipping '%s' (no pressure curve)\n",
                     gimp_object_get_name (device_info));
            continue;
          }

        g_print ("  Resetting device: %s\n",
                 gimp_object_get_name (device_info));

        gimp_curve_reset (pressure_curve, FALSE);
      }
  }

  g_print ("All pressure curves reset to linear (x1.0)\n");
  g_print ("All per-brush curves cleared\n");
  g_print ("==========================================\n\n");

  stylus_editor_save_brush_curves (editor);
  gimp_devices_save (editor->context->gimp, TRUE);
  
  g_signal_emit (editor, stylus_editor_signals[NATURAL_CURVE_REQUESTED], 0);
}

static void
stylus_editor_calibrate_clicked (GtkButton    *button,
                                  StylusEditor *editor)
{
  GtkWidget *dialog;
  GtkWidget *toplevel;

  if (!editor->context)
    return;

  toplevel = gtk_widget_get_toplevel (GTK_WIDGET (editor));
  if (!GTK_IS_WINDOW (toplevel))
    toplevel = NULL;

  dialog = gimp_pressure_calibration_dialog_new (editor->context, toplevel);

  g_signal_connect (dialog, "response",
                    G_CALLBACK (gtk_widget_destroy), NULL);

  gtk_widget_show (dialog);
  gtk_dialog_run (GTK_DIALOG (dialog));
}

static void
stylus_editor_toggle_curve_clicked (GtkButton    *button,
                                     StylusEditor *editor)
{
  GimpDeviceManager *device_manager;
  GimpDeviceInfo    *device_info;
  GimpCurve         *pressure_curve;

  /* Toggle the global state */
  custom_curves_enabled = !custom_curves_enabled;

  /* Update the label */
  if (custom_curves_enabled)
    {
      gtk_label_set_text (GTK_LABEL (editor->curve_state_label),
                          _("Custom Curves: Enabled"));
      g_print ("\n=== Custom Curves ENABLED ===\n");
    }
  else
    {
      gtk_label_set_text (GTK_LABEL (editor->curve_state_label),
                          _("Custom Curves: Disabled"));
      g_print ("\n=== Custom Curves DISABLED ===\n");
    }

  if (!editor->context)
    return;

  device_manager = gimp_devices_get_manager (editor->context->gimp);
  if (!device_manager)
    return;

  device_info = gimp_device_manager_get_current_device (device_manager);
  if (!device_info)
    return;

  pressure_curve = gimp_device_info_get_curve (device_info,
                                               GDK_AXIS_PRESSURE);
  if (!pressure_curve)
    return;

  /* Apply the current state to actual device curve (affects drawing) */
  if (!custom_curves_enabled)
    {
      /* Disable: Set device curve to linear (but keep display curve) */
      g_print ("  Setting device curve to linear (display unchanged)\n");
      
      /* Save current curve for display before resetting */
      if (editor->display_curve)
        g_object_unref (editor->display_curve);
      editor->display_curve = GIMP_CURVE (gimp_config_duplicate (GIMP_CONFIG (pressure_curve)));
      
      /* Reset actual device curve to linear */
      gimp_curve_reset (pressure_curve, FALSE);
    }
  else
    {
      /* Enable: Restore the appropriate curve to device */
      if (editor->current_brush)
        {
          const gchar *brush_name;
          GimpCurve   *stored_curve;

          brush_name = gimp_object_get_name (GIMP_OBJECT (editor->current_brush));
          stored_curve = g_hash_table_lookup (editor->brush_curves,
                                               brush_name);

          if (stored_curve)
            {
              g_print ("  Restoring stored curve for brush '%s'\n",
                       brush_name);
              gimp_config_copy (GIMP_CONFIG (stored_curve),
                                GIMP_CONFIG (pressure_curve),
                                GIMP_CONFIG_PARAM_SERIALIZE);
            }
          else if (editor->global_default_curve)
            {
              g_print ("  Restoring global default curve\n");
              gimp_config_copy (GIMP_CONFIG (editor->global_default_curve),
                                GIMP_CONFIG (pressure_curve),
                                GIMP_CONFIG_PARAM_SERIALIZE);
            }
          else
            {
              g_print ("  No custom curve to restore, using linear\n");
              gimp_curve_reset (pressure_curve, FALSE);
            }
        }
      else if (editor->global_default_curve)
        {
          g_print ("  Restoring global default curve\n");
          gimp_config_copy (GIMP_CONFIG (editor->global_default_curve),
                            GIMP_CONFIG (pressure_curve),
                            GIMP_CONFIG_PARAM_SERIALIZE);
        }
      else
        {
          g_print ("  No custom curve to restore, using linear\n");
          gimp_curve_reset (pressure_curve, FALSE);
        }
      
      /* Update display curve to match */
      if (editor->display_curve)
        g_object_unref (editor->display_curve);
      editor->display_curve = GIMP_CURVE (gimp_config_duplicate (GIMP_CONFIG (pressure_curve)));
    }

  /* Update the curve view to show display curve */
  if (editor->curve_view && editor->display_curve)
    {
      gimp_curve_view_set_curve (GIMP_CURVE_VIEW (editor->curve_view),
                                 editor->display_curve, NULL);
    }

  g_print ("================================\n\n");
}

static gboolean
stylus_editor_block_events (GtkWidget *widget,
                             GdkEvent  *event,
                             gpointer   user_data)
{
  if (event->type == GDK_BUTTON_PRESS)
    {
      GimpCurveView *curve_view = GIMP_CURVE_VIEW (widget);
      GimpCurve     *curve;

      curve = gimp_curve_view_get_curve (curve_view);
      if (curve)
        {
          gint n_points = gimp_curve_get_n_points (curve);

          if (n_points >= 3)
            {
              gint    width;
              gint    height;
              gdouble curve_x;
              gdouble curve_y;
              gint    point_index;

              g_print ("points:%d\n", n_points);
              width = gtk_widget_get_allocated_width (widget);
              height = gtk_widget_get_allocated_height (widget);
              curve_x = event->button.x / (gdouble) width;
              curve_y = 1.0 - (event->button.y / (gdouble) height);
              point_index = gimp_curve_get_closest_point (curve,
                                                          curve_x,
                                                          curve_y,
                                                          0.05);

              if (point_index < 0)
                {
                  return TRUE;
                }
            }
        }
    }

  return FALSE;
}

static gboolean
stylus_editor_update_pressure (gpointer data)
{
  StylusEditor      *editor = STYLUS_EDITOR (data);
  GimpDeviceManager *device_manager;
  GimpDeviceInfo    *device_info;
  GimpCoords         coords;
  gchar             *text;
  GdkWindow         *window;

  if (!editor->context)
    return TRUE;

  device_manager = gimp_devices_get_manager (editor->context->gimp);
  if (!device_manager)
    return TRUE;

  device_info = gimp_device_manager_get_current_device (device_manager);
  if (!device_info)
    return TRUE;

  editor->last_active_device = device_info;

  window = gtk_widget_get_window (GTK_WIDGET (editor));

  if (window)
    {
      gimp_device_info_get_device_coords (device_info, window, &coords);

      text = g_strdup_printf (_("%s - Pressure: %.3f"),
                              gimp_object_get_name (GIMP_OBJECT (device_info)),
                              coords.pressure);
      gtk_label_set_text (GTK_LABEL (editor->pressure_label), text);
      g_free (text);
    }
  else
    {
      text = g_strdup_printf (_("Device: %s"),
                              gimp_object_get_name (GIMP_OBJECT (device_info)));
      gtk_label_set_text (GTK_LABEL (editor->pressure_label), text);
      g_free (text);
    }

  return TRUE;
}

/*  Private functions  */

static gboolean
stylus_editor_curve_draw (GtkWidget    *widget,
                           cairo_t      *cr,
                           StylusEditor *editor)
{
  PangoLayout *layout;
  gint         width;
  gint         height;
  gint         layout_width;
  gint         layout_height;
  const gint   border = 6;  /* Match the border used in gimpcurveview.c */

  /* Get widget dimensions */
  width = gtk_widget_get_allocated_width (widget);
  height = gtk_widget_get_allocated_height (widget);

  /* Create pango layout for text rendering */
  layout = gtk_widget_create_pango_layout (widget, NULL);

  /* Set white color for labels */
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);

  /* Draw X-axis label (centered horizontally at bottom) */
  pango_layout_set_text (layout, _("pen pressure"), -1);
  pango_layout_get_pixel_size (layout, &layout_width, &layout_height);

  cairo_move_to (cr,
                 border + (width / 2.0) - (layout_width / 2.0),
                 height - border - layout_height);
  pango_cairo_show_layout (cr, layout);

  /* Draw Y-axis label (centered vertically on left side, rotated) */
  pango_layout_set_text (layout, _("pressure"), -1);
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


/*  Public functions  */

GtkWidget *
stylus_editor_new (GimpContext     *context,
                   GimpMenuFactory *menu_factory)
{
  GtkWidget         *editor_widget;
  StylusEditor      *editor;
  GimpDeviceManager *device_manager;
  GimpDeviceInfo    *device_info;
  GimpCurve         *pressure_curve;
  GimpCurve         *stored_curve;
  const gchar       *brush_name;

  g_return_val_if_fail (GIMP_IS_CONTEXT (context), NULL);

  editor_widget = g_object_new (STYLUS_TYPE_EDITOR, "context", context,
                                "menu-identifier", "<StylusEditor>", NULL);

  editor = STYLUS_EDITOR (editor_widget);

  editor->context = context;
  g_object_ref (editor->context);

  global_stylus_editor = editor;

  g_signal_connect (editor->context, "brush-changed",
                    G_CALLBACK (stylus_editor_brush_changed),
                    editor);

  editor->current_brush = gimp_context_get_brush (context);

  stylus_editor_load_brush_curves (editor);

  device_manager = gimp_devices_get_manager (context->gimp);
  if (device_manager)
    {
      device_info = gimp_device_manager_get_current_device (device_manager);
      if (device_info && editor->curve_view)
        {
          stored_curve = NULL;

          if (editor->current_brush)
            {
              brush_name = gimp_object_get_name (GIMP_OBJECT (editor->current_brush));
              stored_curve = g_hash_table_lookup (editor->brush_curves,
                                                   brush_name);

              /* Only apply custom curve if enabled */
              if (!custom_curves_enabled)
                {
                  g_print ("Custom curves DISABLED - using linear curve\n");
                  /* Don't apply any custom curve */
                  stored_curve = NULL;
                }

              if (stored_curve)
                {
                  g_print ("Applying stored curve for initial brush '%s'\n",
                           brush_name);
                  pressure_curve = gimp_device_info_get_curve (device_info,
                                                               GDK_AXIS_PRESSURE);
                  if (pressure_curve)
                    {
                      gimp_config_copy (GIMP_CONFIG (stored_curve),
                                        GIMP_CONFIG (pressure_curve),
                                        GIMP_CONFIG_PARAM_SERIALIZE);
                    }
                }
            }

          pressure_curve = gimp_device_info_get_curve (device_info,
                                                       GDK_AXIS_PRESSURE);
          if (pressure_curve)
            {
              /* Store curve for display */
              if (stored_curve)
                {
                  if (editor->display_curve)
                    g_object_unref (editor->display_curve);
                  editor->display_curve = GIMP_CURVE (gimp_config_duplicate (GIMP_CONFIG (stored_curve)));
                }
              else
                {
                  if (editor->display_curve)
                    g_object_unref (editor->display_curve);
                  editor->display_curve = GIMP_CURVE (gimp_config_duplicate (GIMP_CONFIG (pressure_curve)));
                }

              /* If custom curves are disabled, set device to linear */
              if (!custom_curves_enabled)
                {
                  g_print ("Custom curves DISABLED - setting device to linear\n");
                  gimp_curve_reset (pressure_curve, FALSE);
                }

              /* Always show the display curve (custom curve) in view */
              gimp_curve_view_set_curve (GIMP_CURVE_VIEW (editor->curve_view),
                                         editor->display_curve, NULL);

              /* Connect dirty signal to display curve (what user edits) */
              g_signal_connect (editor->display_curve, "dirty",
                                G_CALLBACK (stylus_editor_curve_dirty),
                                editor);

              editor->curve_view_device = device_info;

              g_print ("Curve view set to device: %s\n",
                       gimp_object_get_name (GIMP_OBJECT (device_info)));
            }
        }
    }

  return editor_widget;
}

static void
stylus_editor_curve_dirty (GimpCurve    *curve,
                            StylusEditor *editor)
{
  GimpDeviceManager *device_manager;
  GimpDeviceInfo    *device_info;
  GimpCurve         *device_curve;

  if (!editor->context)
    return;

  g_print ("\n=== Curve manually edited, saving for current brush ===\n");

  /* This is called when display_curve is edited */
  /* If custom curves are enabled, apply to device curve immediately */
  if (custom_curves_enabled)
    {
      device_manager = gimp_devices_get_manager (editor->context->gimp);
      if (device_manager)
        {
          device_info = gimp_device_manager_get_current_device (device_manager);
          if (device_info)
            {
              device_curve = gimp_device_info_get_curve (device_info,
                                                         GDK_AXIS_PRESSURE);
              if (device_curve)
                {
                  g_print ("  Applying edited curve to device\n");
                  gimp_config_copy (GIMP_CONFIG (curve),
                                    GIMP_CONFIG (device_curve),
                                    GIMP_CONFIG_PARAM_SERIALIZE);
                }
            }
        }
    }
  else
    {
      g_print ("  Custom curves DISABLED - curve saved but not applied\n");
    }

  stylus_editor_store_curve (editor->context->gimp, curve, FALSE);
  gimp_devices_save (editor->context->gimp, TRUE);

  g_print ("=======================================================\n\n");
}

static void
stylus_editor_brush_changed (GimpContext  *context,
                              GimpBrush    *brush,
                              StylusEditor *editor)
{
  GimpDeviceManager *device_manager;
  GimpDeviceInfo    *device_info;
  GimpCurve         *stored_curve;
  GimpCurve         *device_curve;
  const gchar       *brush_name;

  if (!brush)
    return;

  brush_name = gimp_object_get_name (GIMP_OBJECT (brush));
  g_print ("\n=== Brush Changed to: %s ===\n", brush_name);
  g_print ("  Hash table size: %d\n",
           g_hash_table_size (editor->brush_curves));

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

  g_print ("  Device: %s\n",
           gimp_object_get_name (GIMP_OBJECT (device_info)));

  device_curve = gimp_device_info_get_curve (device_info,
                                             GDK_AXIS_PRESSURE);
  if (!device_curve)
    {
      g_print ("  ERROR: No device curve!\n");
      return;
    }

  /* Disconnect old display curve signal */
  if (editor->display_curve)
    {
      g_signal_handlers_disconnect_by_func (editor->display_curve,
                                            stylus_editor_curve_dirty,
                                            editor);
    }

  stored_curve = g_hash_table_lookup (editor->brush_curves, brush_name);
  g_print ("  Stored curve lookup result: %p\n",
           (void *) stored_curve);

  /* Update display curve */
  if (stored_curve)
    {
      if (editor->display_curve)
        g_object_unref (editor->display_curve);
      editor->display_curve = GIMP_CURVE (gimp_config_duplicate (GIMP_CONFIG (stored_curve)));
    }

  /* Check if custom curves are enabled */
  if (!custom_curves_enabled)
    {
      g_print ("  Custom curves DISABLED - applying linear curve to device\n");
      gimp_curve_reset (device_curve, FALSE);
      
      /* But keep showing custom curve in view */
      if (editor->curve_view && editor->display_curve)
        {
          gimp_curve_view_set_curve (GIMP_CURVE_VIEW (editor->curve_view),
                                     editor->display_curve, NULL);
        }
    }
  else if (stored_curve)
    {
      g_print ("  Found stored curve for brush '%s', applying it\n",
               brush_name);

      gimp_config_copy (GIMP_CONFIG (stored_curve),
                        GIMP_CONFIG (device_curve),
                        GIMP_CONFIG_PARAM_SERIALIZE);
    }
  else
    {
      g_print ("  No per-brush curve for '%s'\n", brush_name);

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
    }

  /* Update display curve if not disabled */
  if (custom_curves_enabled && editor->display_curve)
    {
      g_object_unref (editor->display_curve);
      editor->display_curve = GIMP_CURVE (gimp_config_duplicate (GIMP_CONFIG (device_curve)));
    }

  /* Disconnect old display curve signal if exists */
  if (editor->display_curve)
    {
      g_signal_handlers_disconnect_by_func (editor->display_curve,
                                            stylus_editor_curve_dirty,
                                            editor);
    }

  /* Always show display curve in view */
  if (editor->curve_view && editor->display_curve)
    {
      gimp_curve_view_set_curve (GIMP_CURVE_VIEW (editor->curve_view),
                                 editor->display_curve, NULL);
    }

  /* Connect dirty signal to display curve (what user edits) */
  if (editor->display_curve)
    {
      g_signal_connect (editor->display_curve, "dirty",
                        G_CALLBACK (stylus_editor_curve_dirty),
                        editor);
    }

  editor->current_brush = brush;
  editor->curve_view_device = device_info;

  g_print ("============================\n\n");
}

gdouble
stylus_editor_get_power (Gimp *gimp)
{
  return current_power_setting;
}

void
stylus_editor_store_curve (Gimp       *gimp,
                            GimpCurve  *curve,
                            gboolean    apply_to_all)
{
  if (!global_stylus_editor)
    {
      g_print ("Warning: No stylus editor instance available "
               "for per-brush curves\n");
      return;
    }

  g_print ("\n=== stylus_editor_store_curve called ===\n");
  g_print ("  apply_to_all: %s\n", apply_to_all ? "YES" : "NO");
  g_print ("  Hash table size BEFORE: %d\n",
           g_hash_table_size (global_stylus_editor->brush_curves));

  if (apply_to_all)
    {
      g_print ("  Storing curve for ALL brushes "
               "(setting as global default)\n");

      g_hash_table_remove_all (global_stylus_editor->brush_curves);

      if (global_stylus_editor->global_default_curve)
        g_object_unref (global_stylus_editor->global_default_curve);

      global_stylus_editor->global_default_curve =
        GIMP_CURVE (gimp_config_duplicate (GIMP_CONFIG (curve)));

      g_print ("  Set global default curve\n");
    }
  else if (global_stylus_editor->current_brush)
    {
      const gchar *brush_name;
      GimpCurve   *curve_copy;

      brush_name = gimp_object_get_name (GIMP_OBJECT (global_stylus_editor->current_brush));

      g_print ("  Storing curve for brush '%s'\n", brush_name);

      curve_copy = GIMP_CURVE (gimp_config_duplicate (GIMP_CONFIG (curve)));

      g_hash_table_insert (global_stylus_editor->brush_curves,
                           g_strdup (brush_name),
                           curve_copy);

      g_print ("  Successfully stored in hash table\n");
    }
  else
    {
      g_print ("  ERROR: No current brush set!\n");
    }

  g_print ("  Hash table size AFTER: %d\n",
           g_hash_table_size (global_stylus_editor->brush_curves));
  g_print ("======================================\n\n");

  stylus_editor_save_brush_curves (global_stylus_editor);
}

const gchar *
stylus_editor_get_current_brush_name (Gimp *gimp)
{
  if (!global_stylus_editor || !global_stylus_editor->current_brush)
    return NULL;

  return gimp_object_get_name (GIMP_OBJECT (global_stylus_editor->current_brush));
}

gboolean
stylus_editor_are_custom_curves_enabled (void)
{
  return custom_curves_enabled;
}

void
stylus_editor_update_display_curve (Gimp      *gimp,
                                    GimpCurve *curve)
{
  GimpDeviceManager *device_manager;
  GimpDeviceInfo    *device_info;
  GimpCurve         *device_curve;

  if (!global_stylus_editor)
    return;

  /* Disconnect old signal handler */
  if (global_stylus_editor->display_curve)
    {
      g_signal_handlers_disconnect_by_func (global_stylus_editor->display_curve,
                                            stylus_editor_curve_dirty,
                                            global_stylus_editor);
      g_object_unref (global_stylus_editor->display_curve);
    }

  /* Update the display curve with the new calibration */
  global_stylus_editor->display_curve =
    GIMP_CURVE (gimp_config_duplicate (GIMP_CONFIG (curve)));

  /* If custom curves are enabled, also update the device curve */
  if (custom_curves_enabled && global_stylus_editor->context)
    {
      device_manager = gimp_devices_get_manager (global_stylus_editor->context->gimp);
      if (device_manager)
        {
          device_info = gimp_device_manager_get_current_device (device_manager);
          if (device_info)
            {
              device_curve = gimp_device_info_get_curve (device_info,
                                                         GDK_AXIS_PRESSURE);
              if (device_curve)
                {
                  g_print ("Custom curves ENABLED - applying to device immediately\n");
                  gimp_config_copy (GIMP_CONFIG (curve),
                                    GIMP_CONFIG (device_curve),
                                    GIMP_CONFIG_PARAM_SERIALIZE);
                }
            }
        }
    }

  /* Update the curve view to show the new curve */
  if (global_stylus_editor->curve_view)
    {
      gimp_curve_view_set_curve (GIMP_CURVE_VIEW (global_stylus_editor->curve_view),
                                 global_stylus_editor->display_curve, NULL);
    }

  /* Connect dirty signal to new display curve */
  g_signal_connect (global_stylus_editor->display_curve, "dirty",
                    G_CALLBACK (stylus_editor_curve_dirty),
                    global_stylus_editor);

  g_print ("Display curve updated with new calibration\n");
}

static void
stylus_editor_save_brush_curves (StylusEditor *editor)
{
  GimpConfigWriter *writer;
  GFile            *file;
  GList            *keys;
  GList            *iter;

  if (!editor || !editor->context)
    return;

  file = gimp_directory_file ("brushcurvesrc", NULL);

  g_print ("\n=== Saving brush curves ===\n");
  g_print ("Writing '%s'\n", gimp_file_get_utf8_name (file));

  writer = gimp_config_writer_new_from_file (file,
                                             TRUE,
                                             "GIMP brush curves\n\n"
                                             "This file stores per-brush "
                                             "pressure curves.",
                                             NULL);
  g_object_unref (file);

  if (!writer)
    return;

  keys = g_hash_table_get_keys (editor->brush_curves);

  for (iter = keys; iter; iter = iter->next)
    {
      const gchar *brush_name = iter->data;
      GimpCurve   *curve;

      curve = g_hash_table_lookup (editor->brush_curves, brush_name);

      g_print ("  Saving brush '%s' -> curve %p\n",
               brush_name, (void *) curve);

      if (curve)
        {
          gimp_config_writer_open (writer, "brush-curve");
          gimp_config_writer_string (writer, brush_name);

          gimp_config_writer_open (writer, "curve");
          gimp_config_serialize_properties (GIMP_CONFIG (curve), writer);
          gimp_config_writer_close (writer);

          gimp_config_writer_close (writer);
        }
    }

  g_list_free (keys);

  if (editor->global_default_curve)
    {
      g_print ("  Saving global default curve -> %p\n",
               (void *) editor->global_default_curve);

      gimp_config_writer_open (writer, "global-default-curve");

      gimp_config_writer_open (writer, "curve");
      gimp_config_serialize_properties (GIMP_CONFIG (editor->global_default_curve),
                                        writer);
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

static void
stylus_editor_load_brush_curves (StylusEditor *editor)
{
  GFile      *file;
  GScanner   *scanner;
  GTokenType  token;
  GList      *loaded_keys;
  GList      *loaded_iter;

  if (!editor || !editor->context)
    return;

  file = gimp_directory_file ("brushcurvesrc", NULL);

  g_print ("\n=== Loading brush curves ===\n");
  g_print ("Parsing '%s'\n", gimp_file_get_utf8_name (file));

  scanner = gimp_scanner_new_file (file, NULL);
  g_object_unref (file);

  if (!scanner)
    {
      g_print ("No saved brush curves found "
               "(this is normal on first run)\n");
      return;
    }

  g_scanner_scope_add_symbol (scanner, 0, "brush-curve",
                               GINT_TO_POINTER (0));
  g_scanner_scope_add_symbol (scanner, 0, "global-default-curve",
                               GINT_TO_POINTER (2));
  g_scanner_scope_add_symbol (scanner, 0, "curve",
                               GINT_TO_POINTER (1));

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
          scanner->value.v_symbol == GINT_TO_POINTER (0))  /* brush-curve */
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
              curve = GIMP_CURVE (gimp_curve_new ("brush curve"));

              token = g_scanner_get_next_token (scanner);
              if (token == G_TOKEN_LEFT_PAREN)
                {
                  token = g_scanner_get_next_token (scanner);
                  if (token == G_TOKEN_SYMBOL &&
                      scanner->value.v_symbol == GINT_TO_POINTER (1))
                    {
                      g_print ("  Found curve data block\n");

                      if (gimp_config_deserialize_properties (GIMP_CONFIG (curve),
                                                             scanner,
                                                             1))
                        {
                          g_hash_table_insert (editor->brush_curves,
                                              brush_name,
                                              curve);

                          g_print ("  Successfully loaded and stored curve for brush '%s' -> %p\n",
                                  brush_name, (void*)curve);

                          token = g_scanner_get_next_token (scanner);
                          g_print ("  After deserialize, consumed token: %d (expecting %d = RIGHT_PAREN)\n",
                                  token, G_TOKEN_RIGHT_PAREN);

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
               scanner->value.v_symbol == GINT_TO_POINTER (2))
        {
          GimpCurve *curve;

          g_print ("  Found global-default-curve\n");

          token = g_scanner_get_next_token (scanner);
          if (token == G_TOKEN_LEFT_PAREN)
            {
              token = g_scanner_get_next_token (scanner);
              if (token == G_TOKEN_SYMBOL &&
                  scanner->value.v_symbol == GINT_TO_POINTER (1))
                {
                  g_print ("  Found curve data block for global default\n");

                  curve = GIMP_CURVE (gimp_curve_new ("global default curve"));

                  if (gimp_config_deserialize_properties (GIMP_CONFIG (curve),
                                                         scanner,
                                                         1))
                    {
                      if (editor->global_default_curve)
                        g_object_unref (editor->global_default_curve);

                      editor->global_default_curve = curve;

                      g_print ("  Successfully loaded global default curve -> %p\n",
                              (void*)curve);

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