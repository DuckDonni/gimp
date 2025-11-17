#ifndef __STYLUS_EDITOR_H__
#define __STYLUS_EDITOR_H__

#include "gimpeditor.h"

G_BEGIN_DECLS

#define STYLUS_TYPE_EDITOR (stylus_editor_get_type ())
#define STYLUS_EDITOR(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), STYLUS_TYPE_EDITOR, StylusEditor))
#define STYLUS_EDITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), STYLUS_TYPE_EDITOR, StylusEditorClass))
#define STYLUS_IS_EDITOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), STYLUS_TYPE_EDITOR))
#define STYLUS_IS_EDITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), STYLUS_TYPE_EDITOR))
#define STYLUS_EDITOR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), STYLUS_TYPE_EDITOR, StylusEditorClass))

typedef struct _StylusEditor      StylusEditor;
typedef struct _StylusEditorClass StylusEditorClass;

struct _StylusEditor
{
  GimpEditor parent_instance;

  GtkAdjustment *slider_adjustment;
  GtkWidget     *slider_scale;
  GtkWidget     *natural_curve_button;
  GtkWidget     *calibrate_button;
  GtkWidget     *pressure_label;
  GtkWidget     *curve_view;
  GtkWidget     *preset_combo;
  GtkWidget     *reset_all_button;
  GtkWidget     *toggle_curve_button;
  GtkWidget     *curve_state_label;
  GimpContext   *context;
  GimpDeviceInfo *last_active_device;
  GimpDeviceInfo *curve_view_device;
  GHashTable     *brush_curves;
  GimpBrush      *current_brush;
  GimpCurve      *global_default_curve;
  GimpCurve      *display_curve;  /* Curve shown in view (unchanged by toggle) */
};

struct _StylusEditorClass

{
  GimpEditorClass parent_class;

  void (* natural_curve_requested) (StylusEditor *editor);
};

GType          stylus_editor_get_type                (void) G_GNUC_CONST;

GtkWidget    * stylus_editor_new                     (GimpContext     *context,
                                                      GimpMenuFactory *menu_factory);

gdouble        stylus_editor_get_power               (Gimp            *gimp);

void           stylus_editor_store_curve             (Gimp            *gimp,
                                                      GimpCurve       *curve,
                                                      gboolean         apply_to_all);

const gchar  * stylus_editor_get_current_brush_name  (Gimp            *gimp);

gboolean       stylus_editor_are_custom_curves_enabled (void);

void           stylus_editor_update_display_curve    (Gimp            *gimp,
                                                      GimpCurve       *curve);

G_END_DECLS

#endif /* __STYLUS_EDITOR_H__ */