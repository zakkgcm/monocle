/* MonocleThumbpane - thumnail scroll view for monocle */
#ifndef __MONOCLE_THUMBPANE_H__
#define __MONOCLE_THUMBPANE_H__

#include <glib-object.h>
#include <gtk/gtk.h>

#define MONOCLE_TYPE_THUMBPANE               (monocle_thumbpane_get_type())
#define MONOCLE_THUMBPANE(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONOCLE_TYPE_THUMBPANE, MonocleThumbpane))
#define MONOCLE_IS_THUMBPANE(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MONOCLE_TYPE_THUMBPANE))
#define MONOCLE_THUMBPANE_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), MONOCLE_TYPE_THUMBPANE, MonocleThumbpaneClass))
#define MONOCLE_IS_THUMBPANE_CLASS(klass)    (G_TYPE_CHECK_VLASS_TYPE ((klass), MONOCLE_TYPE_THUMBPANE))
#define MONOCLE_THUMBPANE_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), MONOCLE_TYPE_THUMBPANE, MonocleThumbpaneClass))

typedef struct _MonocleThumbpane         MonocleThumbpane;
typedef struct _MonocleThumbpaneClass    MonocleThumbpaneClass;

/* So if I understand this right I should be deriving from container classes, not higher level things like treeview */
struct _MonocleThumbpane {
    GtkScrolledWindow widget;
};

struct _MonocleThumbpaneClass {
    GtkScrolledWindowClass parent_class;

    void (* monocle_thumbpane) (MonocleThumbpane *mt);
};

GType monocle_thumbpane_get_type (void);

MonocleThumbpane *monocle_thumbpane_new ();

void monocle_thumbpane_add_image    (MonocleThumbpane *self, gchar *filename);
void monocle_thumbpane_add_many     (MonocleThumbpane *self, GSList *filenames);
void monocle_thumbpane_add_folder   (MonocleThumbpane *self, gchar *folder, gboolean recursive);

void monocle_thumbpane_remove           (MonocleThumbpane *self, GtkTreeIter *row);
void monocle_thumbpane_remove_many      (MonocleThumbpane *self, GList *row_refs);
void monocle_thumbpane_remove_current   (MonocleThumbpane *self);
void monocle_thumbpane_remove_all       (MonocleThumbpane *self);

void monocle_thumbpane_sort_by_name     (MonocleThumbpane *self);
void monocle_thumbpane_sort_by_date     (MonocleThumbpane *self);
void monocle_thumbpane_sort_by_size     (MonocleThumbpane *self);
void monocle_thumbpane_sort_order_ascending      (MonocleThumbpane *self);
void monocle_thumbpane_sort_order_descending     (MonocleThumbpane *self);

void monocle_thumbpane_set_num_threads (MonocleThumbpane *self, gint num_threads);
gint monocle_thumbpane_get_num_threads (MonocleThumbpane *self);
#endif /*__MONOCLE_THUMBPANE_H__*/

