/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimppressurecalibrationdialog.c
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <gegl.h>
#include <gtk/gtk.h>
#include <math.h>

#include "libgimpwidgets/gimpwidgets.h"
#include "libgimpmath/gimpmath.h"

#include "widgets-types.h"

#include "core/gimp.h"
#include "core/gimpcontext.h"
#include "core/gimpcurve.h"

#include "gimpdevices.h"
#include "gimpdevicemanager.h"
#include "gimpdeviceinfo.h"
#include "gimpdeviceinfo-coords.h"
#include "gimpstyluseditor.h"

#include "gimp-intl.h"



#include "gimppressurecalibrationdialog.h"

static void gimp_pressure_calibration_dialog_finalize (GObject *object);
static gboolean drawing_area_draw (GtkWidget *widget, cairo_t *cr, GimpPressureCalibrationDialog *dialog);
static gboolean drawing_area_button_press (GtkWidget *widget, GdkEventButton *event, GimpPressureCalibrationDialog *dialog);
static gboolean drawing_area_button_release (GtkWidget *widget, GdkEventButton *event, GimpPressureCalibrationDialog *dialog);
static gboolean drawing_area_motion_notify (GtkWidget *widget, GdkEventMotion *event, GimpPressureCalibrationDialog *dialog);
static void apply_button_clicked (GtkButton *button, GimpPressureCalibrationDialog *dialog);
static void clear_button_clicked (GtkButton *button, GimpPressureCalibrationDialog *dialog);
static void apply_all_checkbox_toggled (GtkToggleButton *toggle, GimpPressureCalibrationDialog *dialog);

/* ===============================================
 * TYPE DEFINITION
 * =============================================== */
G_DEFINE_TYPE (GimpPressureCalibrationDialog, gimp_pressure_calibration_dialog, GTK_TYPE_DIALOG)

#define parent_class gimp_pressure_calibration_dialog_parent_class

enum
{
  CURVE_APPLIED,
  LAST_SIGNAL
};

static guint dialog_signals[LAST_SIGNAL] = { 0 };

static void
gimp_pressure_calibration_dialog_class_init (GimpPressureCalibrationDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gimp_pressure_calibration_dialog_finalize;

  dialog_signals[CURVE_APPLIED] =
    g_signal_new ("curve-applied",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (GimpPressureCalibrationDialogClass, curve_applied),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
gimp_pressure_calibration_dialog_init (GimpPressureCalibrationDialog *dialog)
{
  GtkWidget *content_area;
  GtkWidget *main_vbox;
  GtkWidget *frame;
  GtkWidget *button_box;

  dialog->context = NULL;
  dialog->target_device = NULL;
  dialog->recording = FALSE;
  dialog->pressure_samples = g_array_new (FALSE, FALSE, sizeof (gdouble));
  dialog->velocity_samples = g_array_new (FALSE, FALSE, sizeof (gdouble));
  dialog->last_event_time = 0;
  dialog->surface = NULL;
  dialog->last_x = 0.0;
  dialog->last_y = 0.0;
  dialog->is_drawing = FALSE;

  gtk_window_set_title (GTK_WINDOW (dialog), _("Pressure Calibration"));
  gtk_window_set_default_size (GTK_WINDOW (dialog), 500, 400);
  gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);

  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  gtk_box_pack_start (GTK_BOX (content_area), main_vbox, TRUE, TRUE, 0);
  gtk_widget_show (main_vbox);

  dialog->status_label = gtk_label_new (_("Draw naturally on the scratchpad below with your stylus.\n"
                                         "Use your normal drawing pressure. Recording starts when you begin drawing."));
  gtk_label_set_line_wrap (GTK_LABEL (dialog->status_label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (dialog->status_label), 0.0);
  gtk_box_pack_start (GTK_BOX (main_vbox), dialog->status_label, FALSE, FALSE, 0);
  gtk_widget_show (dialog->status_label);

  frame = gtk_frame_new (_("Scratchpad"));
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  dialog->drawing_area = gtk_drawing_area_new ();
  gtk_widget_set_size_request (dialog->drawing_area, 400, 300);
  gtk_container_add (GTK_CONTAINER (frame), dialog->drawing_area);
  gtk_widget_show (dialog->drawing_area);

  gtk_widget_add_events (dialog->drawing_area,
                        GDK_POINTER_MOTION_MASK |
                        GDK_BUTTON_PRESS_MASK |
                        GDK_BUTTON_RELEASE_MASK);

  g_signal_connect (dialog->drawing_area, "draw",
                   G_CALLBACK (drawing_area_draw), dialog);
  g_signal_connect (dialog->drawing_area, "button-press-event",
                   G_CALLBACK (drawing_area_button_press), dialog);
  g_signal_connect (dialog->drawing_area, "button-release-event",
                   G_CALLBACK (drawing_area_button_release), dialog);
  g_signal_connect (dialog->drawing_area, "motion-notify-event",
                   G_CALLBACK (drawing_area_motion_notify), dialog);

  button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (main_vbox), button_box, FALSE, FALSE, 0);
  gtk_widget_show (button_box);

  dialog->clear_button = gtk_button_new_with_label (_("Clear"));
  gtk_box_pack_start (GTK_BOX (button_box), dialog->clear_button, FALSE, FALSE, 0);
  gtk_widget_show (dialog->clear_button);
  g_signal_connect (dialog->clear_button, "clicked",
                   G_CALLBACK (clear_button_clicked), dialog);

  dialog->apply_button = gtk_button_new_with_label (_("Apply Calibration"));
  gtk_widget_set_sensitive (dialog->apply_button, FALSE);
  gtk_box_pack_start (GTK_BOX (button_box), dialog->apply_button, FALSE, FALSE, 0);
  gtk_widget_show (dialog->apply_button);
  g_signal_connect (dialog->apply_button, "clicked",
                   G_CALLBACK (apply_button_clicked), dialog);

  dialog->apply_all_checkbox = gtk_check_button_new_with_label (_("Apply to only selected brush"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->apply_all_checkbox), TRUE);
  gtk_box_pack_start (GTK_BOX (main_vbox), dialog->apply_all_checkbox, FALSE, FALSE, 6);
  gtk_widget_show (dialog->apply_all_checkbox);

  dialog->apply_to_all_brushes = TRUE;

  g_signal_connect (dialog->apply_all_checkbox, "toggled",
                   G_CALLBACK (apply_all_checkbox_toggled), dialog);

  gtk_dialog_add_button (GTK_DIALOG (dialog), _("Close"), GTK_RESPONSE_CLOSE);
}

static void
gimp_pressure_calibration_dialog_finalize (GObject *object)
{
  GimpPressureCalibrationDialog *dialog = GIMP_PRESSURE_CALIBRATION_DIALOG (object);

  if (dialog->pressure_samples)
    {
      g_array_free (dialog->pressure_samples, TRUE);
      dialog->pressure_samples = NULL;
    }

  if (dialog->velocity_samples)
    {
      g_array_free (dialog->velocity_samples, TRUE);
      dialog->velocity_samples = NULL;
    }

  if (dialog->surface)
    {
      cairo_surface_destroy (dialog->surface);
      dialog->surface = NULL;
    }

  if (dialog->context)
    {
      g_object_unref (dialog->context);
      dialog->context = NULL;
    }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
drawing_area_draw (GtkWidget *widget,
                  cairo_t   *cr,
                  GimpPressureCalibrationDialog *dialog)
{
  GtkAllocation allocation;

  gtk_widget_get_allocation (widget, &allocation);

  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_paint (cr);

  if (dialog->surface)
    {
      cairo_set_source_surface (cr, dialog->surface, 0, 0);
      cairo_paint (cr);
    }

  cairo_set_source_rgb (cr, 0.7, 0.7, 0.7);
  cairo_set_line_width (cr, 1.0);
  cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
  cairo_stroke (cr);

  return FALSE;
}

static gboolean
drawing_area_button_press (GtkWidget      *widget,
                           GdkEventButton *event,
                           GimpPressureCalibrationDialog *dialog)
{
  GimpDeviceManager *device_manager;

  if (!dialog->recording)
    {
      dialog->recording = TRUE;

      if (dialog->context)
        {
          device_manager = gimp_devices_get_manager (dialog->context->gimp);
          if (device_manager)
            {
              dialog->target_device = gimp_device_manager_get_current_device (device_manager);

              if (dialog->target_device)
                {
                  g_print ("Calibration will target device: %s\n",
                          gimp_object_get_name (dialog->target_device));
                }
            }
        }

      g_print ("\n=== VELOCITY TRACKING STARTED ===\n");

      gtk_label_set_text (GTK_LABEL (dialog->status_label),
                         _("Recording... Draw multiple strokes."));
    }

  dialog->is_drawing = TRUE;
  dialog->last_x = event->x;
  dialog->last_y = event->y;
  dialog->last_event_time = 0;

  return TRUE;
}

static gboolean
drawing_area_button_release (GtkWidget      *widget,
                             GdkEventButton *event,
                             GimpPressureCalibrationDialog *dialog)
{
  dialog->is_drawing = FALSE;

  if (dialog->recording && dialog->pressure_samples->len > 0)
    {
      gchar *text;
      text = g_strdup_printf (_("Collected %d samples so far. Draw more strokes or click 'Apply Calibration'."),
                             dialog->pressure_samples->len);
      gtk_label_set_text (GTK_LABEL (dialog->status_label), text);
      g_free (text);

      if (dialog->velocity_samples->len > 0)
        {
          gdouble min_velocity = G_MAXDOUBLE;
          gdouble max_velocity = 0.0;
          gdouble avg_velocity = 0.0;

          for (guint i = 0; i < dialog->velocity_samples->len; i++)
            {
              gdouble v = g_array_index (dialog->velocity_samples, gdouble, i);
              avg_velocity += v;
              if (v < min_velocity) min_velocity = v;
              if (v > max_velocity) max_velocity = v;
            }
          avg_velocity /= dialog->velocity_samples->len;

          g_print ("\n=== STROKE COMPLETE - CUMULATIVE STATS ===\n");
          g_print ("Total velocity samples: %d\n", dialog->velocity_samples->len);
          g_print ("Min velocity: %.2f px/s\n", min_velocity);
          g_print ("Max velocity: %.2f px/s\n", max_velocity);
          g_print ("Avg velocity: %.2f px/s\n", avg_velocity);
          g_print ("==========================================\n\n");
        }

      gtk_widget_set_sensitive (dialog->apply_button, TRUE);
    }

  return TRUE;
}

static gboolean
drawing_area_motion_notify (GtkWidget      *widget,
                            GdkEventMotion *event,
                            GimpPressureCalibrationDialog *dialog)
{
  cairo_t *cr;
  gdouble pressure;
  GtkAllocation allocation;
  guint32 current_time;
  gdouble time_delta;
  gdouble distance;
  gdouble velocity;
  gdouble dx, dy;

  if (!dialog->recording || !dialog->is_drawing)
    return FALSE;

  if (!gdk_event_get_axis ((GdkEvent *) event, GDK_AXIS_PRESSURE, &pressure))
    {
      pressure = 0.5;
    }



  current_time = event->time;

  if (dialog->last_event_time > 0)
    {
      time_delta = (current_time - dialog->last_event_time) / 1000.0;

      dx = event->x - dialog->last_x;
      dy = event->y - dialog->last_y;
      distance = sqrt (dx * dx + dy * dy);

      if (time_delta > 0.0)
        {
          velocity = distance / time_delta;

          g_array_append_val (dialog->pressure_samples, pressure);
          g_array_append_val (dialog->velocity_samples, velocity);

          g_print ("Time Delta: %.4f sec | Distance: %.2f px | Velocity: %.2f px/s | Pressure: %.3f\n",
                   time_delta, distance, velocity, pressure);
        }
    }

  dialog->last_event_time = current_time;

  if (!dialog->surface)
    {
      gtk_widget_get_allocation (widget, &allocation);
      dialog->surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                                    allocation.width,
                                                    allocation.height);
      cr = cairo_create (dialog->surface);
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      cairo_paint (cr);
      cairo_destroy (cr);
    }

  cr = cairo_create (dialog->surface);

  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_set_line_width (cr, 2.0 + pressure * 8.0);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to (cr, dialog->last_x, dialog->last_y);
  cairo_line_to (cr, event->x, event->y);
  cairo_stroke (cr);


  cairo_destroy (cr);

  dialog->last_x = event->x;
  dialog->last_y = event->y;

  gtk_widget_queue_draw (widget);

  return TRUE;
}

static void
apply_all_checkbox_toggled (GtkToggleButton *toggle,
                            GimpPressureCalibrationDialog *dialog)
{
  dialog->apply_to_all_brushes = gtk_toggle_button_get_active (toggle);

  g_print ("Apply to only selected brush: %s\n",
          dialog->apply_to_all_brushes ? "YES" : "NO");
}

static void
clear_button_clicked (GtkButton *button,
                     GimpPressureCalibrationDialog *dialog)
{
  g_array_set_size (dialog->pressure_samples, 0);
  g_array_set_size (dialog->velocity_samples, 0);

  g_print ("\n=== DATA CLEARED ===\n");

  if (dialog->surface)
    {
      cairo_t *cr;
      cr = cairo_create (dialog->surface);
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      cairo_paint (cr);
      cairo_destroy (cr);
    }

  dialog->recording = FALSE;
  gtk_label_set_text (GTK_LABEL (dialog->status_label),
                     _("Draw naturally on the scratchpad below with your stylus.\n"
                       "Use your normal drawing pressure. Recording starts when you begin drawing."));
  gtk_widget_set_sensitive (dialog->apply_button, FALSE);

  gtk_widget_queue_draw (dialog->drawing_area);
}
static gint
compare_double(const gdouble *a, const gdouble *b)
{
  gdouble val_a = *(const gdouble *) a;
  gdouble val_b = *(const gdouble *) b;

  return (val_a > val_b) - (val_a < val_b);
}

static void
apply_button_clicked (GtkButton *button,
                     GimpPressureCalibrationDialog *dialog)
{
  GimpDeviceManager *device_manager;
  GimpDeviceInfo *device_info;
  GimpCurve *pressure_curve;
  gdouble min_pressure;
  gdouble max_pressure;
  gchar *text;
  gdouble exponent;
  gdouble min_velocity = 0.0;
  gdouble max_velocity = 0.0;
  gdouble avg_velocity = 0.0;
  gdouble velocity_strength;
  gdouble velocity_max = 0.0;
  gdouble velocity_min = 0.0;
  gint p_len = 0;
  gint v_len = 0;
  GArray *V;
  GArray *sortedVelocity;
  GArray *sorted_pressures;
  gdouble e = M_E;

  if (dialog->pressure_samples->len < 10)
    {
      gtk_label_set_text (GTK_LABEL (dialog->status_label),
                         _("Not enough samples. Draw more strokes and try again."));
      return;
    }


  min_pressure = 1.0;
  max_pressure = 0.0;


  for (guint i = 0; i < dialog->pressure_samples->len; i++)
    {
      gdouble p = g_array_index (dialog->pressure_samples, gdouble, i);
      if (p < min_pressure) min_pressure = p;
      if (p > max_pressure) max_pressure = p;
    }



  {
    sorted_pressures = g_array_sized_new (FALSE, FALSE, sizeof (gdouble),
                                                   dialog->pressure_samples->len);


    for (guint i = 0; i < dialog->pressure_samples->len; i++)
      {
        gdouble p = g_array_index (dialog->pressure_samples, gdouble, i);
        g_array_append_val (sorted_pressures, p);
      }

    g_array_sort (sorted_pressures, (GCompareFunc) compare_double);



  }

  min_velocity = G_MAXDOUBLE;
  max_velocity = 0.0;
  avg_velocity = 0.0;

  if (dialog->velocity_samples->len > 0)
    {
      for (guint i = 0; i < dialog->velocity_samples->len; i++)
        {
          gdouble v = g_array_index (dialog->velocity_samples, gdouble, i);
          if (v < min_velocity) min_velocity = v;
          if (v > max_velocity) max_velocity = v;
          avg_velocity += v;
        }
      avg_velocity /= dialog->velocity_samples->len;
    }
  else
    {
      min_velocity = 0.0;
      max_velocity = 1000.0;
      avg_velocity = 500.0;
    }

  exponent = stylus_editor_get_power (dialog->context->gimp);

  if (dialog->velocity_samples->len > 0 && max_velocity > min_velocity)
    {
      gdouble normalized_avg_vel = (avg_velocity - min_velocity) / (max_velocity - min_velocity);
      velocity_strength = 1.0 - (normalized_avg_vel * 0.2);
    }
  else
    {
      velocity_strength = 1.0;
    }

    p_len = dialog->pressure_samples->len;
    v_len = dialog->velocity_samples->len;

    V = g_array_sized_new (FALSE, FALSE, sizeof (gdouble), v_len);
    sortedVelocity = g_array_sized_new (FALSE, FALSE, sizeof (gdouble), v_len);


    for (guint i = 0; i < v_len; i++)
      {
        gdouble v = g_array_index (dialog->velocity_samples, gdouble, i);
        g_array_append_val (sortedVelocity, v);
      }

    g_array_sort (sortedVelocity, (GCompareFunc) compare_double);
    velocity_min = g_array_index (sortedVelocity, gdouble, 0);
    velocity_max = g_array_index (sortedVelocity, gdouble, sortedVelocity->len - 1);
    g_print ("velocity_min: %f, velocity_max: %f\n", velocity_min, velocity_max);
    g_print ("Pressure_min: %f, Pressure_max: %f\n", min_pressure, max_pressure);
    for (guint i = 0; i < v_len; i++)
      {
        gdouble v = g_array_index (dialog->velocity_samples, gdouble, i);

        gdouble Vn = 1 - ((v - velocity_min) / (velocity_max - velocity_min));

        g_array_append_val (V, Vn);
      }
    g_array_free (sortedVelocity, TRUE);

    for (guint i = 0; i < V->len; i++)
      {
        g_print ("V %d: %f\n", i, g_array_index (V, gdouble, i));
      }

  if (dialog->context)
    {
      device_manager = gimp_devices_get_manager (dialog->context->gimp);
      if (device_manager)
        {
          GimpContainer *container = GIMP_CONTAINER (device_manager);
          GList *list;

          for (list = GIMP_LIST (container)->queue->head; list; list = g_list_next (list))
            {
              device_info = GIMP_DEVICE_INFO (list->data);

              pressure_curve = gimp_device_info_get_curve (device_info, GDK_AXIS_PRESSURE);
              if (!pressure_curve)
                {
                  g_print ("  Skipping '%s' (no pressure curve)\n", gimp_object_get_name (device_info));
                  continue;
                }

              g_print ("  Applying sigmoid calibration to device: %s\n", gimp_object_get_name (device_info));

              gimp_curve_set_curve_type (pressure_curve, GIMP_CURVE_SMOOTH);
              gimp_curve_clear_points (pressure_curve);



              // Fit logistic function to collected pressure data
              // Logistic function: f(x) = 1 / (1 + e^(-k(x - x0)))

              // Calculate statistics from collected pressure samples
              gdouble median_pressure;
              gdouble q1_pressure, q3_pressure;
              gdouble iqr;  // Interquartile range
              gdouble mean_pressure = 0.0;
              gdouble variance = 0.0;
              gdouble std_dev;



              // Calculate mean
              for (guint i = 0; i < dialog->pressure_samples->len; i++)
                {
                  gdouble p = g_array_index (dialog->pressure_samples, gdouble, i);
                  mean_pressure += p;
                }
              mean_pressure /= dialog->pressure_samples->len;

              // Calculate standard deviation
              for (guint i = 0; i < dialog->pressure_samples->len; i++)
                {
                  gdouble p = g_array_index (dialog->pressure_samples, gdouble, i);
                  gdouble diff = p - mean_pressure;
                  variance += diff * diff;
                }
              variance /= dialog->pressure_samples->len;
              std_dev = sqrt (variance);

              // Removal of outliers
              // SD STUFF

              GArray *removed_indices = g_array_new(FALSE, FALSE, sizeof(guint));

              for (guint i = 0; i < dialog->pressure_samples->len; i++)
              {
                  gdouble p = g_array_index(dialog->pressure_samples, gdouble, i);
                  if (fabs(p - mean_pressure) <= 3.0 * std_dev)
                      g_array_append_val(removed_indices, i);
              }

              if (dialog->pressure_samples->len - removed_indices->len >= 5)
              {
                for (guint i = 0; i < removed_indices->len; i++)
                {
                  guint index = g_array_index(removed_indices, guint, i);
                  g_array_remove_index(dialog->pressure_samples, index);
                }
              }


              // Calculate median and quartiles from sorted pressures
              guint n = sorted_pressures->len;
              if (n % 2 == 0)
                {
                  median_pressure = (g_array_index (sorted_pressures, gdouble, n/2 - 1) +
                                    g_array_index (sorted_pressures, gdouble, n/2)) / 2.0;
                }
              else
                {
                  median_pressure = g_array_index (sorted_pressures, gdouble, n/2);
                }





              // Calculate quartiles
              guint q1_idx = n / 4;
              guint q3_idx = (3 * n) / 4;
              q1_pressure = g_array_index (sorted_pressures, gdouble, q1_idx);
              q3_pressure = g_array_index (sorted_pressures, gdouble, q3_idx);
              iqr = q3_pressure - q1_pressure;

              // Fit logistic parameters based on collected data
              // Use median as the midpoint (where curve is steepest)
              gdouble x0 = median_pressure;

              // Use IQR or std_dev to determine steepness
              // Wider spread = gentler curve (lower k), narrower spread = steeper curve (higher k)
              // Scale k based on the spread relative to the full range
              gdouble pressure_range = max_pressure - min_pressure;
              gdouble spread_measure = (iqr > 0.0) ? iqr : std_dev;

              // Normalize spread to [0, 1] range and map to k values
              // k ranges from ~4 (gentle) to ~12 (steep)
              gdouble normalized_spread = (pressure_range > 0.0) ?
                                         (spread_measure / pressure_range) : 0.5;
              gdouble k = 4.0 + (1.0 - normalized_spread) * 8.0;  // k in [4, 12]

              // Clamp k to reasonable bounds
              k = CLAMP (k, 4.0, 12.0);

              g_print ("  Fitted logistic: k=%.2f, x0=%.3f (median=%.3f, spread=%.3f)\n",
                      k, x0, median_pressure, spread_measure);


              //PSEUDOCODE
              /*
              remove outliers 3xsd
              mean
              sd

              filtered ar
              for each val x in data
                if abs(x-mean) <= 3 * sd
                  append x in filtered



              ------

              implement moving average


              */




              // Create curve points using fitted logistic function
              // Map input pressure [0, 1] to output pressure [0, 1] with S-shape

              guint n_points = 5;
              for (guint i = 0; i <= n_points; i++)
                {
                  gdouble x = i / (gdouble) n_points;  // Input pressure from 0.0 to 1.0

                  // Logistic function: f(x) = 1 / (1 + e^(-k(x - x0)))
                  gdouble exp_term = -k * (x - x0);
                  gdouble y = 1.0 / (1.0 + pow(e, exp_term));

                  // Clamp to valid range (should already be in [0, 1])
                  y = CLAMP (y, 0.0, 1.0);

                  gimp_curve_add_point (pressure_curve, x, y);
                }
            }
        }
    }
      g_array_free (sorted_pressures, TRUE);
  g_print ("Sigmoid calibration applied to all devices!\n");
  g_print ("=========================================\n\n");



  if (dialog->context && device_info)
    {
      GimpCurve *applied_curve = gimp_device_info_get_curve (device_info, GDK_AXIS_PRESSURE);
      if (applied_curve)
        {
          stylus_editor_store_curve (dialog->context->gimp, applied_curve,
                                     !dialog->apply_to_all_brushes);
        }
    }

  if (dialog->context)
    {
      gimp_devices_save (dialog->context->gimp, TRUE);
      g_print ("Device configurations saved to devicerc\n");
    }

  if (velocity_strength < 0.99)
    {
      text = g_strdup_printf (_("Calibration applied!\n"
                               "Power=%.2f, Velocity scaling=%.2f (faster→thinner)"),
                             exponent, velocity_strength);
    }
  else
    {
      text = g_strdup_printf (_("Calibration applied!\n"
                               "Power=%.2f (no velocity adjustment)"),
                             exponent);
    }
  gtk_label_set_text (GTK_LABEL (dialog->status_label), text);
  g_free (text);

  g_array_set_size (dialog->pressure_samples, 0);
  g_array_set_size (dialog->velocity_samples, 0);
  dialog->recording = FALSE;

  g_signal_emit (dialog, dialog_signals[CURVE_APPLIED], 0);
}

GtkWidget *
gimp_pressure_calibration_dialog_new (GimpContext *context,
                                     GtkWidget   *parent)
{
  GimpPressureCalibrationDialog *dialog;

  g_return_val_if_fail (GIMP_IS_CONTEXT (context), NULL);

  dialog = g_object_new (GIMP_TYPE_PRESSURE_CALIBRATION_DIALOG, NULL);

  dialog->context = context;
  g_object_ref (dialog->context);

  if (parent)
    {
      gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent));
      gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
    }

  return GTK_WIDGET (dialog);
}

