/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimppressurecalibrationdialog.h
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

#ifndef __GIMP_PRESSURE_CALIBRATION_DIALOG_H__
#define __GIMP_PRESSURE_CALIBRATION_DIALOG_H__

#include <gtk/gtk.h>
#include "core/gimpcontext.h"

G_BEGIN_DECLS

#define GIMP_TYPE_PRESSURE_CALIBRATION_DIALOG            (gimp_pressure_calibration_dialog_get_type ())
#define GIMP_PRESSURE_CALIBRATION_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GIMP_TYPE_PRESSURE_CALIBRATION_DIALOG, GimpPressureCalibrationDialog))
#define GIMP_PRESSURE_CALIBRATION_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GIMP_TYPE_PRESSURE_CALIBRATION_DIALOG, GimpPressureCalibrationDialogClass))
#define GIMP_IS_PRESSURE_CALIBRATION_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GIMP_TYPE_PRESSURE_CALIBRATION_DIALOG))
#define GIMP_IS_PRESSURE_CALIBRATION_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GIMP_TYPE_PRESSURE_CALIBRATION_DIALOG))
#define GIMP_PRESSURE_CALIBRATION_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIMP_TYPE_PRESSURE_CALIBRATION_DIALOG, GimpPressureCalibrationDialogClass))

typedef struct _GimpPressureCalibrationDialog      GimpPressureCalibrationDialog;
typedef struct _GimpPressureCalibrationDialogClass GimpPressureCalibrationDialogClass;

struct _GimpPressureCalibrationDialog
{
  GtkDialog parent_instance;

  GtkWidget *drawing_area;
  GtkWidget *status_label;
  GtkWidget *apply_button;
  GtkWidget *clear_button;
  GtkWidget *apply_all_checkbox;
  GimpContext *context;
  GimpDeviceInfo *target_device;
  gboolean recording;
  GArray *pressure_samples;
  gboolean apply_to_all_brushes;
  GArray *velocity_samples;
  guint32 last_event_time;
  cairo_surface_t *surface;
  gdouble last_x;
  gdouble last_y;
  gboolean is_drawing;
};

struct _GimpPressureCalibrationDialogClass
{
  GtkDialogClass parent_class;

  void (* curve_applied) (GimpPressureCalibrationDialog *dialog);
};

GType          gimp_pressure_calibration_dialog_get_type (void) G_GNUC_CONST;

GtkWidget    * gimp_pressure_calibration_dialog_new       (GimpContext *context,
                                                           GtkWidget   *parent);

G_END_DECLS

#endif /* __GIMP_PRESSURE_CALIBRATION_DIALOG_H__ */

