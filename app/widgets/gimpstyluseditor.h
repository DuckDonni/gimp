#ifndef __STYLUS_EDITOR_H__
#define __STYLUS_EDITOR_H__

#include "gimpeditor.h"

G_BEGIN_DECLS

#define STYLUS_TYPE_DOCK (stylus_editor_get_type ())
#define STYLUS_EDITOR(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), STYLUS_EDITOR_DOCK, StylusEditor))
#define STYLUS_EDITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), STYLUS_EDITOR_DOCK, StylusEditorClass))
#define STYLUS_IS_DOCK(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), STYLUS_EDITOR_DOCK))
#define STYLUS_IS_DOCK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), STYLUS_EDITOR_DOCK))
#define STYLUS_EDITOR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), STYLUS_EDITOR_DOCK, StylusEditorClass))

typedef struct _StylusEditor      StylusEditor;
typedef struct _StylusEditorClass StylusEditorClass;

struct _StylusEditor
{
  GimpEditor parent_instance;

  // Slider Components
  GtkAdjustment *slider_adjustment;
  GtkWidget     *slider_scale;

  // Natural Curve button
  GtkWidget     *natural_curve_button;
  
  // Calibrate button
  GtkWidget     *calibrate_button;
  
  // Pressure display
  GtkWidget     *pressure_label;
  
  // Curve view widget
  GtkWidget     *curve_view;
  
  // Context for device access
  GimpContext   *context;
  
  // Last device that was actively used (for targeting curve changes)
  GimpDeviceInfo *last_active_device;
  
  // Natural curve toggle state
  gboolean      natural_curve_enabled;
};

struct _StylusEditorClass

{
  GimpEditorClass parent_class;

  /* Emitted when Natural Curve is requested by user */
  void (* natural_curve_requested) (StylusEditor *editor);
};

GType stylus_editor_get_type (void) G_GNUC_CONST;

GtkWidget *stylus_editor_new (GimpContext *context, GimpMenuFactory *menu_factory);

G_END_DECLS

#endif /* __STYLUS_EDITOR_H__ */