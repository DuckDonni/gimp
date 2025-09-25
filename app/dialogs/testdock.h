#ifndef __TESTDOCK_H__
#define __TESTDOCK_H__

#include "config.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _GimpDialogFactory GimpDialogFactory;
typedef struct _GimpContext       GimpContext;
typedef struct _GimpUIManager     GimpUIManager;

/* Matches GimpDialogNewFunc exactly */
GtkWidget *
testdock_new (GimpDialogFactory *factory,
              GimpContext       *context,
              GimpUIManager     *ui_manager,
              gint               view_size);

G_END_DECLS

#endif /* __TESTDOCK_H__ */
