/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
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

 /* ===============================================
  * FORWARD DECLARATIONS
  * =============================================== */
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

 /* ===============================================
  * CLASS INITIALIZATION
  * =============================================== */
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

 /* ===============================================
  * INSTANCE INITIALIZATION
  * =============================================== */
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

   /* Instructions label */
   dialog->status_label = gtk_label_new (_("Draw naturally on the scratchpad below with your stylus.\n"
                                          "Use your normal drawing pressure. Recording starts when you begin drawing."));
   gtk_label_set_line_wrap (GTK_LABEL (dialog->status_label), TRUE);
   gtk_label_set_xalign (GTK_LABEL (dialog->status_label), 0.0);
   gtk_box_pack_start (GTK_BOX (main_vbox), dialog->status_label, FALSE, FALSE, 0);
   gtk_widget_show (dialog->status_label);

   /* Drawing area frame */
   frame = gtk_frame_new (_("Scratchpad"));
   gtk_box_pack_start (GTK_BOX (main_vbox), frame, TRUE, TRUE, 0);
   gtk_widget_show (frame);

   /* Drawing area */
   dialog->drawing_area = gtk_drawing_area_new ();
   gtk_widget_set_size_request (dialog->drawing_area, 400, 300);
   gtk_container_add (GTK_CONTAINER (frame), dialog->drawing_area);
   gtk_widget_show (dialog->drawing_area);

   /* Set up drawing area events */
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

   /* Button box */
   button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start (GTK_BOX (main_vbox), button_box, FALSE, FALSE, 0);
   gtk_widget_show (button_box);

   /* Clear button */
   dialog->clear_button = gtk_button_new_with_label (_("Clear"));
   gtk_box_pack_start (GTK_BOX (button_box), dialog->clear_button, FALSE, FALSE, 0);
   gtk_widget_show (dialog->clear_button);
   g_signal_connect (dialog->clear_button, "clicked",
                    G_CALLBACK (clear_button_clicked), dialog);

   /* Apply button */
   dialog->apply_button = gtk_button_new_with_label (_("Apply Calibration"));
   gtk_widget_set_sensitive (dialog->apply_button, FALSE);
   gtk_box_pack_start (GTK_BOX (button_box), dialog->apply_button, FALSE, FALSE, 0);
   gtk_widget_show (dialog->apply_button);
   g_signal_connect (dialog->apply_button, "clicked",
                    G_CALLBACK (apply_button_clicked), dialog);

   /* "Apply to only selected brush" checkbox - checked by default */
   dialog->apply_all_checkbox = gtk_check_button_new_with_label (_("Apply to only selected brush"));
   gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->apply_all_checkbox), TRUE);
   gtk_box_pack_start (GTK_BOX (main_vbox), dialog->apply_all_checkbox, FALSE, FALSE, 6);
   gtk_widget_show (dialog->apply_all_checkbox);

   /* Initialize state - TRUE means apply to current brush only */
   dialog->apply_to_all_brushes = TRUE;

   /* Connect checkbox to update state */
   g_signal_connect (dialog->apply_all_checkbox, "toggled",
                    G_CALLBACK (apply_all_checkbox_toggled), dialog);

   /* Add Close button to dialog */
   gtk_dialog_add_button (GTK_DIALOG (dialog), _("Close"), GTK_RESPONSE_CLOSE);
 }

 /* ===============================================
  * OBJECT LIFECYCLE MANAGEMENT
  * =============================================== */
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

 /* ===============================================
  * DRAWING AREA CALLBACKS
  * =============================================== */
 static gboolean
 drawing_area_draw (GtkWidget *widget,
                   cairo_t   *cr,
                   GimpPressureCalibrationDialog *dialog)
 {
   GtkAllocation allocation;

   gtk_widget_get_allocation (widget, &allocation);

   /* Draw white background */
   cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
   cairo_paint (cr);

   /* Draw the surface with user's strokes */
   if (dialog->surface)
     {
       cairo_set_source_surface (cr, dialog->surface, 0, 0);
       cairo_paint (cr);
     }

   /* Draw border */
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

   /* Start recording when first stroke begins */
   if (!dialog->recording)
     {
       dialog->recording = TRUE;

       /* Capture the current active device when starting recording */
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

   /* Start a new stroke */
   dialog->is_drawing = TRUE;
   dialog->last_x = event->x;
   dialog->last_y = event->y;
   dialog->last_event_time = 0;  /* Reset timing for new stroke */

   return TRUE;
 }

 static gboolean
 drawing_area_button_release (GtkWidget      *widget,
                              GdkEventButton *event,
                              GimpPressureCalibrationDialog *dialog)
 {
   /* End the current stroke */
   dialog->is_drawing = FALSE;

   /* Update statistics (but keep recording active for more strokes) */
   if (dialog->recording && dialog->pressure_samples->len > 0)
     {
       gchar *text;
       text = g_strdup_printf (_("Collected %d samples so far. Draw more strokes or click 'Apply Calibration'."),
                              dialog->pressure_samples->len);
       gtk_label_set_text (GTK_LABEL (dialog->status_label), text);
       g_free (text);

       /* Print velocity statistics */
       if (dialog->velocity_samples->len > 0)
         {
           gdouble min_velocity = G_MAXDOUBLE;
           gdouble max_velocity = 0.0;
           gdouble avg_velocity = 0.0;
           guint i;

           for (i = 0; i < dialog->velocity_samples->len; i++)
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

       /* Enable apply button if we have enough data */
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

   /* Try to get pressure from the event axis */
   if (!gdk_event_get_axis ((GdkEvent *) event, GDK_AXIS_PRESSURE, &pressure))
     {
       /* No pressure axis available, use default */
       pressure = 0.5;
     }

   /* Store pressure sample */
   g_array_append_val (dialog->pressure_samples, pressure);

   /* ===============================================
    * VELOCITY CALCULATION
    * =============================================== */
   current_time = event->time;  /* Event time in milliseconds */

   if (dialog->last_event_time > 0)
     {
       /* Calculate time delta (convert to seconds) */
       time_delta = (current_time - dialog->last_event_time) / 1000.0;

       /* Calculate distance moved (in pixels) */
       dx = event->x - dialog->last_x;
       dy = event->y - dialog->last_y;
       distance = sqrt (dx * dx + dy * dy);

       /* Calculate velocity (pixels per second) */
       if (time_delta > 0.0)
         {
           velocity = distance / time_delta;

           /* Store velocity */
           g_array_append_val (dialog->velocity_samples, velocity);

           /* Print velocity to console */
           g_print ("Time Delta: %.4f sec | Distance: %.2f px | Velocity: %.2f px/s | Pressure: %.3f\n",
                    time_delta, distance, velocity, pressure);
         }
     }

   dialog->last_event_time = current_time;

   /* Create surface if needed */
   if (!dialog->surface)
     {
       gtk_widget_get_allocation (widget, &allocation);
       dialog->surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                                     allocation.width,
                                                     allocation.height);
       /* Fill with white */
       cr = cairo_create (dialog->surface);
       cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
       cairo_paint (cr);
       cairo_destroy (cr);
     }

   /* Draw stroke on surface */
   cr = cairo_create (dialog->surface);

   /* Draw line from last position to current position */
   cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
   cairo_set_line_width (cr, 2.0 + pressure * 8.0);  /* Width varies with pressure */
   cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
   cairo_move_to (cr, dialog->last_x, dialog->last_y);
   cairo_line_to (cr, event->x, event->y);
   cairo_stroke (cr);

   cairo_destroy (cr);

   /* Update last position for next stroke segment */
   dialog->last_x = event->x;
   dialog->last_y = event->y;

   /* Redraw the widget */
   gtk_widget_queue_draw (widget);

   return TRUE;
 }

 /* ===============================================
  * BUTTON CALLBACKS
  * =============================================== */
 static void
 apply_all_checkbox_toggled (GtkToggleButton *toggle,
                             GimpPressureCalibrationDialog *dialog)
 {
   dialog->apply_to_all_brushes = gtk_toggle_button_get_active (toggle);

   /* Print the state */
   g_print ("Apply to only selected brush: %s\n",
           dialog->apply_to_all_brushes ? "YES" : "NO");
 }

 static void
 clear_button_clicked (GtkButton *button,
                      GimpPressureCalibrationDialog *dialog)
 {
   /* Clear samples */
   g_array_set_size (dialog->pressure_samples, 0);
   g_array_set_size (dialog->velocity_samples, 0);

   g_print ("\n=== DATA CLEARED ===\n");

   /* Clear surface */
   if (dialog->surface)
     {
       cairo_t *cr;
       cr = cairo_create (dialog->surface);
       cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
       cairo_paint (cr);
       cairo_destroy (cr);
     }

   /* Reset UI */
   dialog->recording = FALSE;
   gtk_label_set_text (GTK_LABEL (dialog->status_label),
                      _("Draw naturally on the scratchpad below with your stylus.\n"
                        "Use your normal drawing pressure. Recording starts when you begin drawing."));
   gtk_widget_set_sensitive (dialog->apply_button, FALSE);

   gtk_widget_queue_draw (dialog->drawing_area);
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
   gdouble avg_pressure;
   gdouble sum;
   guint i;
   gchar *text;
   gdouble exponent;
   gdouble min_velocity, max_velocity, avg_velocity;
   gdouble velocity_strength;

   if (dialog->pressure_samples->len < 10)
     {
       gtk_label_set_text (GTK_LABEL (dialog->status_label),
                          _("Not enough samples. Draw more strokes and try again."));
       return;
     }

   /* Analyze pressure data - find min and max */
   min_pressure = 1.0;
   max_pressure = 0.0;
   sum = 0.0;

   for (i = 0; i < dialog->pressure_samples->len; i++)
     {
       gdouble p = g_array_index (dialog->pressure_samples, gdouble, i);
       if (p < min_pressure) min_pressure = p;
       if (p > max_pressure) max_pressure = p;
       sum += p;
     }

   avg_pressure = sum / dialog->pressure_samples->len;

   /* Analyze velocity data */
   min_velocity = G_MAXDOUBLE;
   max_velocity = 0.0;
   avg_velocity = 0.0;

   if (dialog->velocity_samples->len > 0)
     {
       for (i = 0; i < dialog->velocity_samples->len; i++)
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
       /* No velocity data - use defaults */
       min_velocity = 0.0;
       max_velocity = 1000.0;
       avg_velocity = 500.0;
     }

   /* Get power setting from Stylus Editor */
   exponent = stylus_editor_get_power (dialog->context->gimp);

   /* Calculate velocity scaling factor
    * Faster average velocity → lower factor → thinner lines
    * Formula: factor = 1.0 - (normalized_avg_velocity * 0.2)
    * This gives 20% max reduction for very fast strokes
    */
   if (dialog->velocity_samples->len > 0 && max_velocity > min_velocity)
     {
       gdouble normalized_avg_vel = (avg_velocity - min_velocity) / (max_velocity - min_velocity);
       velocity_strength = 1.0 - (normalized_avg_vel * 0.2);  /* Range: 0.8 to 1.0 */
     }
   else
     {
       velocity_strength = 1.0;  /* No velocity data - no adjustment */
     }

   g_print ("\n=== Applying Calibration to ALL devices ===\n");
   g_print ("Pressure analysis: min=%.3f, max=%.3f, avg=%.3f, samples=%d\n",
           min_pressure, max_pressure, avg_pressure, dialog->pressure_samples->len);
   g_print ("Velocity analysis: avg=%.2f px/s → scaling factor=%.3f\n",
           avg_velocity, velocity_strength);
   g_print ("Power setting: %.2f\n", exponent);
   g_print ("Creating curve: y = (x^%.2f) × %.3f (no cutoffs, 0→0, 1→1)\n",
           exponent, velocity_strength);

   /* Get device manager to apply calibration */
   if (dialog->context)
     {
       device_manager = gimp_devices_get_manager (dialog->context->gimp);
       if (device_manager)
         {
           GimpContainer *container = GIMP_CONTAINER (device_manager);
           GList *list;

           /* Apply to all devices (pressure curves are device-level, not per-brush) */
           /* Apply to all devices (pressure curves are device-level, not per-brush) */
           for (list = GIMP_LIST (container)->queue->head; list; list = g_list_next (list))
             {
               device_info = GIMP_DEVICE_INFO (list->data);

               pressure_curve = gimp_device_info_get_curve (device_info, GDK_AXIS_PRESSURE);
               if (!pressure_curve)
                 {
                   g_print ("  Skipping '%s' (no pressure curve)\n", gimp_object_get_name (device_info));
                   continue;
                 }

               g_print ("  Applying calibration to device: %s\n", gimp_object_get_name (device_info));

               /* Build curve from control points (vertices), smooth interpolation */
               gimp_curve_set_curve_type (pressure_curve, GIMP_CURVE_SMOOTH);

               /* Clear existing points and define new vertices following y = (x^exponent) * velocity_strength */
               gimp_curve_clear_points (pressure_curve);

               /* Ensure bounds */
               {
                 gdouble end_y = pow (1.0, exponent) * velocity_strength;
                 if (end_y < 0.0) end_y = 0.0;
                 if (end_y > 1.0) end_y = 1.0;

                 /* Start and end vertices */
                 gimp_curve_add_point (pressure_curve, 0.0, 0.0);
                 gimp_curve_add_point (pressure_curve, 1.0, end_y);

                 /* Add a few intermediate vertices to shape the power curve */
                 {
                   /* Tunable density of control points */
                   const gint n_mid_points = 1; /* produces 8 total points incl. endpoints */

                   for (i = 1; i <= n_mid_points; i++)
                     {
                       gdouble x = (gdouble) i / (n_mid_points + 1); /* evenly spaced in (0,1) */
                       gdouble y = pow (x, exponent) * velocity_strength;

                       if (y < 0.0) y = 0.0;
                       if (y > 1.0) y = 1.0;

                       gimp_curve_add_point (pressure_curve, x, y);
                     }
                 }
               }

               g_print ("    Created curve via vertices: y = (x^%.2f) × %.3f (smooth)\n",
                        exponent, velocity_strength);
             }
         }
     }

   g_print ("Calibration applied to all devices!\n");
   g_print ("=========================================\n\n");

   /* Store curve for current brush or all brushes */
   if (dialog->context && device_info)
     {
       GimpCurve *applied_curve = gimp_device_info_get_curve (device_info, GDK_AXIS_PRESSURE);
       if (applied_curve)
         {
           /* Invert the checkbox logic: checked = current brush only (FALSE), unchecked = all brushes (TRUE) */
           stylus_editor_store_curve (dialog->context->gimp, applied_curve,
                                      !dialog->apply_to_all_brushes);
         }
     }

   /* Save device configurations to persist the curves across sessions */
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

   /* Reset arrays for next calibration session */
   g_array_set_size (dialog->pressure_samples, 0);
   g_array_set_size (dialog->velocity_samples, 0);
   dialog->recording = FALSE;
   //gtk_widget_set_sensitive (dialog->apply_button, TRUE);

   g_signal_emit (dialog, dialog_signals[CURVE_APPLIED], 0);
 }

 /* ===============================================
  * PUBLIC API
  * =============================================== */
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
