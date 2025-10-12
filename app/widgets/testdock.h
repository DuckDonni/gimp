// #ifndef __TEST_DOCK_H__
// #define __TEST_DOCK_H__

// #include "gimpeditor.h"

// G_BEGIN_DECLS

// #define TEST_TYPE_DOCK            (test_dock_get_type ())
// #define TEST_DOCK(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TEST_TYPE_DOCK, TestDock))
// #define TEST_DOCK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TEST_TYPE_DOCK, TestDockClass))
// #define TEST_IS_DOCK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TEST_TYPE_DOCK))
// #define TEST_IS_DOCK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TEST_TYPE_DOCK))
// #define TEST_DOCK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_TYPE_DOCK, TestDockClass))

// typedef struct _TestDock        TestDock;
// typedef struct _TestDockClass   TestDockClass;

// struct _TestDock
// {
//   GimpEditor  parent_instance;
// };

// struct _TestDockClass
// {
//   GimpEditorClass  parent_class;
// };

// GType       test_dock_get_type (void) G_GNUC_CONST;

// GtkWidget * test_dock_new      (GimpContext     *context,
//                                 GimpMenuFactory *menu_factory);

// G_END_DECLS

// #endif /* __TEST_DOCK_H__ */