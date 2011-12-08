/* MonocleThumbPane implementation file
 * manages a list of filenames and their thumbnails
 * author: cheeseum
 * license: see LICENSE
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <glib/gstdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "monoclethumbpane.h"
#include "monoclethumblist.h"

#define MONOCLE_THUMBPANE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), MONOCLE_TYPE_THUMBPANE, MonocleThumbpanePrivate))

typedef struct _MonocleThumbpanePrivate {
    GtkTreeView *treeview;
    MonocleThumblist *thumblist;
    GtkSortType sort_order;

    gint rowcount;
} MonocleThumbpanePrivate;

typedef struct _MonocleThumbpaneFolder {
    gchar *name;
    GtkTreeRowReference *rowref;
} MonocleThumbpaneFolder;

/* Thumbpane keeps a list of the loaded files handy, nothing else really has a use for such a list so there's no need to keep it outside of this widget */

/* TODO: don't inherit scrolled window or something */
G_DEFINE_TYPE(MonocleThumbpane, monocle_thumbpane, GTK_TYPE_SCROLLED_WINDOW)

enum {
    SORT_NAME,
    SORT_DATE,
    SORT_SIZE
};

enum {
    CHANGED_SIGNAL,
    ROWCOUNT_SIGNAL,
    LAST_SIGNAL
};

/*static void monocle_thumbpane_size_allocate (GtkWidget *widget, GtkAllocation *allocation);*/
static gboolean cb_row_selected (GtkTreeSelection *selection, GtkTreeModel *model, GtkTreePath *path, gboolean curpath, gpointer user_data);
static void cb_row_inserted      (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, MonocleThumbpane *self);
static void cb_row_child_toggled (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, MonocleThumbpane *self);
static void cb_row_deleted       (GtkTreeModel *model, GtkTreePath *path, MonocleThumbpane *self);

static gint cb_sort_func (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data);
static gint sort_func_date (gchar *a, gchar *b);
static gint sort_func_size (gchar *a, gchar *b);

static gint get_folder_in_model (MonocleThumbpane *self, MonocleThumbpaneFolder *out, gchar *folder);
static void create_folder_in_model (MonocleThumbpane *self, MonocleThumbpaneFolder *out, gchar *folder);

static guint monocle_thumbpane_signals[LAST_SIGNAL] = { 0 };

static void
monocle_thumbpane_init (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkWidget           *treeview;
    MonocleThumblist    *thumblist;
    GtkTreeViewColumn   *col;
    GtkTreeSelection    *sel;
    GtkCellRenderer     *thumbnailer;

    priv->treeview = NULL;

    thumblist   = monocle_thumblist_new();
    treeview    = gtk_tree_view_new();
    col         = gtk_tree_view_column_new();
    sel         = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    thumbnailer = gtk_cell_renderer_pixbuf_new();

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);

    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
    gtk_tree_view_column_pack_start(col, thumbnailer, TRUE);

    gtk_tree_view_column_add_attribute(col, thumbnailer, "pixbuf", MONOCLE_THUMBLIST_COL_THUMBNAIL);

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(thumblist));

    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(treeview), FALSE); 
    gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(treeview), FALSE);

    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
    gtk_tree_selection_set_select_function(sel, cb_row_selected, self, NULL);

    /*gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(tree), SORT_NAME, cb_sort_func, GINT_TO_POINTER(SORT_NAME), NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(tree), SORT_DATE, cb_sort_func, GINT_TO_POINTER(SORT_DATE), NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(tree), SORT_SIZE, cb_sort_func, GINT_TO_POINTER(SORT_SIZE), NULL);*/

    g_signal_connect_object(G_OBJECT(thumblist), "row-inserted", G_CALLBACK(cb_row_inserted), self, 0);
    g_signal_connect_object(G_OBJECT(thumblist), "row-has-child-toggled", G_CALLBACK(cb_row_child_toggled), self, 0);
    g_signal_connect_object(G_OBJECT(thumblist), "row-deleted",  G_CALLBACK(cb_row_deleted), self, 0);

    priv->sort_order = GTK_SORT_ASCENDING;
    /*gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(thumblist), SORT_NAME, priv->sort_order);*/
    priv->treeview = GTK_TREE_VIEW(treeview);
    priv->thumblist = thumblist;
}

static void
monocle_thumbpane_class_init (MonocleThumbpaneClass *klass) {
    g_type_class_add_private(klass, sizeof(MonocleThumbpanePrivate));

    monocle_thumbpane_signals[CHANGED_SIGNAL] =
            g_signal_new( "image-changed", G_TYPE_FROM_CLASS(klass),
                      G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET(MonocleThumbpaneClass, monocle_thumbpane),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE, 
                      1, G_TYPE_STRING );
    monocle_thumbpane_signals[ROWCOUNT_SIGNAL] =
            g_signal_new( "rowcount-changed", G_TYPE_FROM_CLASS(klass),
                      G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET(MonocleThumbpaneClass, monocle_thumbpane),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 
                      1, G_TYPE_INT );

    }

MonocleThumbpane
*monocle_thumbpane_new () {
    MonocleThumbpane *thumbpane = g_object_new(MONOCLE_TYPE_THUMBPANE, NULL);
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(thumbpane);

    /* fixes GTK_IS_RANGE error */
    gtk_container_add(GTK_CONTAINER(thumbpane), GTK_WIDGET(priv->treeview));
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(thumbpane), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
     
    return thumbpane;
}

void
monocle_thumbpane_add (MonocleThumbpane *self, gchar *filename) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeIter newrow;

    monocle_thumblist_append (priv->thumblist, &newrow, filename);
    
    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), &newrow);
}

/* Add a whole bunch of images (or just two whichever) */
void
monocle_thumbpane_add_many (MonocleThumbpane *self, GSList *filenames) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeIter row;
    
    gtk_tree_view_set_model(priv->treeview, NULL);

    do {
        monocle_thumblist_append (priv->thumblist, &row, (gchar *)filenames->data);
    } while ((filenames = g_slist_next(filenames)) != NULL);

    gtk_tree_view_set_model(priv->treeview, GTK_TREE_MODEL(priv->thumblist));

    /* TODO: make this select the first item in the list */
    /* ^ only if there's nothing else selected~ */
    /*if(gtk_tree_model_filter_convert_child_iter_to_iter(GTK_TREE_MODEL_FILTER(treefilter), &filterrow, &row))
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), &filterrow);
    g_object_unref(treefilter);*/
}

/* Add a whole bunch of images (or just two whichever) from a directory */
/* it's cool because it can be easily made recursive */
/* this might stack overflow if there's a REEEEEEEEEEALY deeply nested folder structure */
/* TODO: move this into another thread or idle loop for FULL CONCURRENCY */
void
monocle_thumbpane_add_folder (MonocleThumbpane *self, gchar *folder, gboolean recursive) {
    GDir *folder_tree;
    GSList *filenames;
    const gchar *filename;
    gchar *filepath;

    filenames = NULL;

    if ((folder_tree = g_dir_open(folder, 0, NULL)) == NULL)
        return;

    while ((filename = g_dir_read_name(folder_tree)) != NULL) {
        /* some check for an image file here
         * want to avoid just checking file extension since it may not be true
         * then again it's simpler */
        /* patch together the full path of the file, 2 is a backslash + nul */
        filepath = g_malloc(strlen(folder) + strlen(filename) + 2);
        sprintf(filepath, "%s/%s", folder, filename);
        
        if (recursive && g_file_test(filepath, G_FILE_TEST_IS_DIR)) {
            monocle_thumbpane_add_folder (self, filepath, TRUE);
        }else if (g_file_test(filepath, G_FILE_TEST_IS_REGULAR)) {
            filenames = g_slist_prepend(filenames, filepath); /* prepend + reverse is quicker */
        }
    }
    filenames = g_slist_reverse(filenames);

    g_dir_close(folder_tree);
    if (filenames != NULL) {
        monocle_thumbpane_add_many(self, filenames);
        do { g_free(filenames->data); } while ((filenames = g_slist_next(filenames)) != NULL);
        g_slist_free(filenames);
    }

    /* TODO: select last folder */
}

/* remove functions take paths/iters in relation to the underlying model */
void
monocle_thumbpane_remove (MonocleThumbpane *self, GtkTreeIter *row) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeIter rowfilter;
    gboolean valid;
    
    valid = monocle_thumblist_remove(GTK_TREE_STORE(priv->thumblist), row);

    if(valid)
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), row);
    else               
        g_signal_emit(G_OBJECT(self), monocle_thumbpane_signals[CHANGED_SIGNAL], 0, NULL);
}

void
monocle_thumbpane_remove_many (MonocleThumbpane *self, GList *row_refs) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreePath *path;
    GtkTreeIter row;
    gboolean valid = FALSE;

    gtk_tree_view_set_model(priv->treeview, NULL);
    
    /* these operate on _priv->thumblist_ which is the actual data containing model */
    while (row_refs != NULL) {
        path = gtk_tree_row_reference_get_path((GtkTreeRowReference *)row_refs->data);
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(priv->thumblist), &row, path)) {
            valid = monocle_thumblist_remove(priv->thumblist, &row);
        }
        row_refs = row_refs->next;
    }
    gtk_tree_view_set_model(priv->treeview, GTK_TREE_MODEL(priv->thumblist));
    
    /* row should be set to the next avaliable valid row */
    if (!valid && !gtk_tree_model_get_iter_first(GTK_TREE_MODEL(priv->thumblist), &row)) {
        /* if we get here then the next row is invalid AND there is no first row */
        g_signal_emit(G_OBJECT(self), monocle_thumbpane_signals[CHANGED_SIGNAL], 0, NULL);
        return;
    }
    
    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), &row);
}

void
monocle_thumbpane_remove_all (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    monocle_thumblist_clear(priv->thumblist);
    g_signal_emit(G_OBJECT(self), monocle_thumbpane_signals[CHANGED_SIGNAL], 0, NULL);
}

/* folder can be NULL to specify the current one */
void
monocle_thumbpane_remove_folder (MonocleThumbpane *self, gchar *folder) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    
    MonocleFolder *next_folder = NULL;
    gboolean valid = FALSE;

    /* XXX: we remove and reapply the model to force updates 
     * this is pretty hackish and not straightforward */
    gtk_tree_view_set_model(priv->treeview, NULL);

    if (!folder)
        valid = monocle_thumblist_remove_current_folder(priv->thumblist, &next_folder);

    if (valid) /* FIXME/HELPME: next_folder is always NULL, idk why */
        monocle_thumblist_select_folder(priv->thumblist, next_folder);
    
    gtk_tree_view_set_model(priv->treeview, GTK_TREE_MODEL(priv->thumblist));
}

void
monocle_thumbpane_remove_current (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeModel *model;
    GList *rows;
    GList *row_refs = NULL;
    
    model = GTK_TREE_MODEL(priv->thumblist);
    rows = gtk_tree_selection_get_selected_rows(
                gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)),
                &model);
    
    /* convert the list of treepaths into row references */
    /* avoids WOAH NELLY when we remove */
    while (rows != NULL) {
        row_refs = g_list_prepend(row_refs,
                                  gtk_tree_row_reference_new(GTK_TREE_MODEL(priv->thumblist),
                                  (GtkTreePath *)rows->data));
        rows = rows->next;
    }
    row_refs = g_list_reverse(row_refs);
    
    g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
    g_list_free (rows);

    monocle_thumbpane_remove_many(self, row_refs);
    g_list_foreach (rows, (GFunc) gtk_tree_row_reference_free, NULL);
    g_list_free(row_refs);
}

void
monocle_thumbpane_next_folder (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);

    gtk_tree_view_set_model(priv->treeview, NULL);
    monocle_thumblist_next_folder(priv->thumblist);
    gtk_tree_view_set_model(priv->treeview, GTK_TREE_MODEL(priv->thumblist));
}

void
monocle_thumbpane_prev_folder (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    
    gtk_tree_view_set_model(priv->treeview, NULL);
    monocle_thumblist_prev_folder(priv->thumblist);
    gtk_tree_view_set_model(priv->treeview, GTK_TREE_MODEL(priv->thumblist));
}

void
monocle_thumbpane_view_folder (MonocleThumbpane *self, gchar *folder) {
}

/* FIXME: the parent "folder rows" are visible here, using the visible func of gtktreemodelfilter does not work
 * only thought to fix it is to make a custom treemodelfilter or copy everything into a liststore */
void
monocle_thumbpane_view_all (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
}

/* These seem redundant, maybe have an arg with some exposed ENUMS or something */
/* FIXME: make these convenience functions that call something else */

void
monocle_thumbpane_sort_by_name (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(priv->thumblist);
    gtk_tree_sortable_set_sort_column_id(sortable, SORT_NAME, priv->sort_order);
}

void
monocle_thumbpane_sort_by_date (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(priv->thumblist);
    gtk_tree_sortable_set_sort_column_id(sortable, SORT_DATE, priv->sort_order);
}

void
monocle_thumbpane_sort_by_size (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(priv->thumblist);
    gtk_tree_sortable_set_sort_column_id(sortable, SORT_SIZE, priv->sort_order);
}

/* These are ugly, they look pretty but they're ugly */
void
monocle_thumbpane_sort_order_ascending (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(priv->thumblist);
    gint sort_type;

    priv->sort_order = GTK_SORT_ASCENDING;
    gtk_tree_sortable_get_sort_column_id(sortable, &sort_type, NULL);
    gtk_tree_sortable_set_sort_column_id(sortable, sort_type, priv->sort_order);
}

void
monocle_thumbpane_sort_order_descending (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(priv->thumblist);
    gint sort_type;
    
    priv->sort_order = GTK_SORT_DESCENDING;
    gtk_tree_sortable_get_sort_column_id(sortable, &sort_type, NULL);
    gtk_tree_sortable_set_sort_column_id(sortable, sort_type, priv->sort_order);
}

void
monocle_thumbpane_set_num_threads (MonocleThumbpane *self, gint num_threads) {
    return;
}

gint
monocle_thumbpane_get_num_threads (MonocleThumbpane *self) {
    return 2;
}

static gboolean
cb_row_selected (GtkTreeSelection *selection,
                 GtkTreeModel *model,
                 GtkTreePath *path,
                 gboolean curpath,
                 gpointer user_data)
{
    /* The "user_data" is actually our thumbpane object */
    MonocleThumbpane *self = MONOCLE_THUMBPANE(user_data);
    GtkTreeIter iter;
    gchar *filename;
    
    /* FIXME: make this not emit if there is more than one selected */
    /* FIXME: make this not emit when browising using ctrl */

    if (!curpath) {
        /* handle this error <- what? */
        if (gtk_tree_model_get_iter(model, &iter, path)) {
            gtk_tree_model_get(model, &iter, MONOCLE_THUMBLIST_COL_FILENAME, &filename, -1);
            /* I think this is ugly, handler should get the filename itself possibly */
            g_signal_emit(G_OBJECT(self), monocle_thumbpane_signals[CHANGED_SIGNAL], 0, filename);
            g_free(filename);
        }
    }
   
    return TRUE;
}

static void
cb_row_inserted (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    priv->rowcount = priv->rowcount + 1;
    g_signal_emit(G_OBJECT(self), monocle_thumbpane_signals[ROWCOUNT_SIGNAL], 0, priv->rowcount);
}

static void
cb_row_child_toggled (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, MonocleThumbpane *self) {
    gboolean is_folder;
    gchar *folder;
}

static void
cb_row_deleted (GtkTreeModel *model, GtkTreePath *path, MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    priv->rowcount = priv->rowcount - 1;
    g_signal_emit(G_OBJECT(self), monocle_thumbpane_signals[ROWCOUNT_SIGNAL], 0, priv->rowcount);
}


/* Sorting Functions */
static gint
cb_sort_func (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data) {
    gint ret = 0;
    gchar *filea, *fileb;
    
    gtk_tree_model_get(model, a, MONOCLE_THUMBLIST_COL_FILENAME, &filea, -1);
    gtk_tree_model_get(model, b, MONOCLE_THUMBLIST_COL_FILENAME, &fileb, -1);

    if (filea == NULL || fileb == NULL) {
        if (filea == NULL && fileb == NULL)
            ret = 0;
        else if (filea == NULL)
            ret = -1;
        else
            ret = 1;
    } else {
        switch (GPOINTER_TO_INT(user_data)) {
            case SORT_NAME: {
                ret = g_ascii_strcasecmp(filea, fileb);
            }
            break;
            case SORT_DATE: {
                ret = sort_func_date(filea, fileb);
            }
            break;
            case SORT_SIZE: {
                ret = sort_func_size(filea, fileb);
            }
            break;
        }
    }
    
    if (filea != NULL)
        g_free(filea);
    if (fileb != NULL)
        g_free(fileb);
    
    return ret;
}

static gint
sort_func_date (gchar *a, gchar *b) {
    GStatBuf stata, statb;
    g_stat((const gchar *)a, &stata);
    g_stat((const gchar *)b, &statb);

    if (stata.st_mtime == statb.st_mtime)
        return 0;
    else if (stata.st_mtime > statb.st_mtime)
        return 1;
    else
        return -1;
}

static gint
sort_func_size (gchar *a, gchar *b) {
    GStatBuf stata, statb;
    g_stat((const gchar *)a, &stata);
    g_stat((const gchar *)b, &statb);

    if (stata.st_size == statb.st_size)
        return 0;
    else if (stata.st_size > statb.st_size)
        return 1;
    else
        return -1;
}
