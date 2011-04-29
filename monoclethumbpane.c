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
#include "utils/md5.h"

#define MONOCLE_THUMBPANE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), MONOCLE_TYPE_THUMBPANE, MonocleThumbpanePrivate))

typedef struct _MonocleThumbpanePrivate {
    GtkTreeView *treeview;
    GtkTreeModel *model; /* the REAL model */
    GtkSortType sort_order;

    GThreadPool *pool;
    gint num_threads;

    gint rowcount;
    GList *folders; /* list of known foldernames and their rowrefs */
    gint curfolder; /* index in the list of folders */
    GdkPixbuf *default_thumb;
} MonocleThumbpanePrivate;

typedef struct _MonocleThumbpaneFolder {
    gchar *name;
    GtkTreeRowReference *rowref;
} MonocleThumbpaneFolder;

/* Thumbpane keeps a list of the loaded files handy, nothing else really has a use for such a list so there's no need to keep it outside of this widget */

/* TODO: don't inherit scrolled window or something */
G_DEFINE_TYPE(MonocleThumbpane, monocle_thumbpane, GTK_TYPE_SCROLLED_WINDOW)

enum {
    COL_FILEPATH,
    COL_THUMBNAIL,
    COL_ISDIR,
    NUM_COLS
};

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
static void thumbnail_thread (GtkTreeRowReference *rowref);
static GdkPixbuf *generate_thumbnail (gchar *filename);

static gchar *md5sum          (gchar *str);
static gchar *encode_file_uri (gchar *str);

static guint monocle_thumbpane_signals[LAST_SIGNAL] = { 0 };

static void
monocle_thumbpane_init (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkWidget           *treeview;
    GtkTreeStore        *tree;
    GtkTreeModel        *treefilter;
    GtkTreeViewColumn   *col;
    GtkTreeSelection    *sel;
    GtkCellRenderer     *thumbnailer;

    priv->treeview = NULL;

    tree        = gtk_tree_store_new( NUM_COLS, G_TYPE_STRING, GDK_TYPE_PIXBUF, GTK_TYPE_BOOL );
    treeview    = gtk_tree_view_new();
    treefilter  = gtk_tree_model_filter_new(GTK_TREE_MODEL(tree), NULL);
    col         = gtk_tree_view_column_new();
    sel         = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    thumbnailer = gtk_cell_renderer_pixbuf_new();

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);

    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
    gtk_tree_view_column_pack_start(col, thumbnailer, TRUE);

    gtk_tree_view_column_add_attribute(col, thumbnailer, "pixbuf", COL_THUMBNAIL);

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), treefilter);

    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(treeview), FALSE); 
    gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(treeview), FALSE);

    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
    gtk_tree_selection_set_select_function(sel, cb_row_selected, self, NULL);

    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(tree), SORT_NAME, cb_sort_func, GINT_TO_POINTER(SORT_NAME), NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(tree), SORT_DATE, cb_sort_func, GINT_TO_POINTER(SORT_DATE), NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(tree), SORT_SIZE, cb_sort_func, GINT_TO_POINTER(SORT_SIZE), NULL);

    g_signal_connect_object(G_OBJECT(tree), "row-inserted", G_CALLBACK(cb_row_inserted), self, 0);
    g_signal_connect_object(G_OBJECT(tree), "row-has-child-toggled", G_CALLBACK(cb_row_child_toggled), self, 0);
    g_signal_connect_object(G_OBJECT(tree), "row-deleted",  G_CALLBACK(cb_row_deleted), self, 0);

    priv->sort_order = GTK_SORT_ASCENDING;
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(tree), SORT_NAME, priv->sort_order);
    priv->treeview = GTK_TREE_VIEW(treeview);
    priv->model = GTK_TREE_MODEL(tree);

    priv->num_threads = 2;
    priv->curfolder = 1;
    
    /* this WILL fail horribly if there is no mystery to be found */
    priv->default_thumb = g_file_test("./Itisamystery.gif", G_FILE_TEST_IS_REGULAR)
                                    ? gdk_pixbuf_new_from_file("./Itisamystery.gif", NULL)
                                    : gdk_pixbuf_new_from_file("/usr/share/monocle/Itisamystery.gif", NULL);

    g_object_unref(treefilter); /* treeview will hold on to it */
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
monocle_thumbpane_add_image (MonocleThumbpane *self, gchar *filename) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GdkPixbuf *thumb;
    GtkTreeStore *tree;
    GtkTreeIter row, filterrow;

    thumb = generate_thumbnail(filename);

    tree = GTK_TREE_STORE(priv->model);
    gtk_tree_store_append(tree, &row, NULL);
    gtk_tree_store_set(tree, &row, COL_FILEPATH, filename, -1);
    gtk_tree_store_set(tree, &row, COL_THUMBNAIL, thumb, -1);
    gtk_tree_store_set(tree, &row, COL_ISDIR, FALSE, -1);

    
    gtk_tree_model_filter_convert_child_iter_to_iter(GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(priv->treeview)), &filterrow, &row);
    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), &filterrow);

    g_object_unref(thumb);
}

/* Add a whole bunch of images (or just two whichever) */
void
monocle_thumbpane_add_many (MonocleThumbpane *self, GSList *filenames) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    /*GdkPixbuf *thumb;*/
    GtkTreeStore *tree;
    GtkTreeModel *treefilter;
    GtkTreeIter row, filterrow;
    gchar *iterstring;

    if (priv->pool == NULL) {
        /* TODO: make the deadpool free itself when finished (after some certain idle time maybe) */
        /* LOOK! LOOK! THERE IS THE DEADPOOL! */
        g_thread_pool_set_max_idle_time(100);
        priv->pool = g_thread_pool_new((GFunc)thumbnail_thread, NULL, priv->num_threads, FALSE, NULL);
    }

    tree = GTK_TREE_STORE(priv->model);
    treefilter = gtk_tree_view_get_model(priv->treeview);
    g_object_ref(treefilter);
    gtk_tree_view_set_model(priv->treeview, NULL);


    do {
        MonocleThumbpaneFolder folderdata;
        gchar *foldername;
        GtkTreeIter folder;

        /* no need to check for validity, file will be appended to the root */
        foldername = g_path_get_dirname((gchar *)filenames->data);
        if (get_folder_in_model(self, &folderdata, foldername) < 0)
            create_folder_in_model(self, &folderdata, foldername);

        gtk_tree_model_get_iter(priv->model, &folder, gtk_tree_row_reference_get_path((GtkTreeRowReference *)folderdata.rowref));
        gtk_tree_store_insert_with_values(tree, &row, &folder,
                                            gtk_tree_model_iter_n_children(GTK_TREE_MODEL(tree), NULL),
                                            COL_FILEPATH, (gchar *)filenames->data,
                                            COL_THUMBNAIL, priv->default_thumb,
                                            COL_ISDIR, FALSE,
                                          -1);
        
        /* POOL POOL */
        /* do I need to wrap this with threads leave/enter? */
        iterstring = gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(tree), &row);
        g_thread_pool_push(priv->pool,
                            (gpointer)gtk_tree_row_reference_new(GTK_TREE_MODEL(tree), gtk_tree_path_new_from_string(iterstring)), 
                            NULL);
        g_free(foldername);
        g_free(iterstring);
    } while ((filenames = g_slist_next(filenames)) != NULL);

    gtk_tree_view_set_model(priv->treeview, treefilter);
    gtk_tree_view_expand_all(priv->treeview);

    /* TODO: make this select the first item in the list */
    /* ^ only if there's nothing else selected~ */
    if(gtk_tree_model_filter_convert_child_iter_to_iter(GTK_TREE_MODEL_FILTER(treefilter), &filterrow, &row))
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), &filterrow);
    g_object_unref(treefilter);
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

    monocle_thumbpane_select_folder(self, folder);
}

/* remove functions take paths/iters in relation to the underlying model */
void
monocle_thumbpane_remove (MonocleThumbpane *self, GtkTreeIter *row) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeIter *nextrow = row; /* should I rather not declare this as a pointer? */
    gboolean valid;
    
    valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(priv->model), nextrow);
    gtk_tree_store_remove(GTK_TREE_STORE(priv->model), row);

    if(valid)
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), nextrow);
    else               
        g_signal_emit(G_OBJECT(self), monocle_thumbpane_signals[CHANGED_SIGNAL], 0, NULL);
}

void
monocle_thumbpane_remove_many (MonocleThumbpane *self, GList *row_refs) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeModel *model;
    GtkTreePath *path;
    GtkTreeIter row, rowfilter;
    gboolean valid = FALSE;

    /* model is the saved model from the treeview (likely a treemodelfilter */
    model = gtk_tree_view_get_model(priv->treeview);
    g_object_ref(model);
    gtk_tree_view_set_model(priv->treeview, NULL);
    
    /* these operate on _priv->model_ which is the actual data containing model */
    while (row_refs != NULL) {
        path = gtk_tree_row_reference_get_path((GtkTreeRowReference *)row_refs->data);
        if (gtk_tree_model_get_iter(priv->model, &row, path)) {
            valid = gtk_tree_store_remove(GTK_TREE_STORE(priv->model), &row);    
        }
        row_refs = row_refs->next;
    }
    gtk_tree_view_set_model(priv->treeview, model);
    
    /* row should be set to the next avaliable valid row */
    if ( !(valid && gtk_tree_model_filter_convert_child_iter_to_iter(GTK_TREE_MODEL_FILTER(model), &rowfilter, &row))) {
        if (!gtk_tree_model_get_iter_first(model, &rowfilter)) {
             /* if we get here then the next row is invalid AND there is no first row */
             g_signal_emit(G_OBJECT(self), monocle_thumbpane_signals[CHANGED_SIGNAL], 0, NULL);
             return;
        }
    }
    
    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), &rowfilter);

}

void
monocle_thumbpane_remove_all (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    gtk_tree_store_clear(GTK_TREE_STORE(priv->model));  
    g_signal_emit(G_OBJECT(self), monocle_thumbpane_signals[CHANGED_SIGNAL], 0, NULL);
}

/* folder can be NULL to specify the current one */
void
monocle_thumbpane_remove_folder (MonocleThumbpane *self, gchar *folder) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GList *elem;
    MonocleThumbpaneFolder folderdata;
    GtkTreeIter iter;

    if (folder == NULL) {
        elem = g_list_nth(priv->folders, priv->curfolder);
        memcpy(&folderdata, (MonocleThumbpaneFolder *)elem->data, sizeof(MonocleThumbpaneFolder));
    } else {
        if(get_folder_in_model(self, &folderdata, folder) < 0)
            return;
    }

    if (!gtk_tree_model_get_iter(priv->model, &iter, gtk_tree_row_reference_get_path(folderdata.rowref)))
            return;
    
    /* has to be done here, row-deleted fires AFTER the row is deleted, so you can't get children */
    /* in gtk+ 3.0 this is not the case though */
    priv->rowcount -= gtk_tree_model_iter_n_children(priv->model, &iter);
    g_signal_emit(G_OBJECT(self), monocle_thumbpane_signals[ROWCOUNT_SIGNAL], 0, priv->rowcount);

    gtk_tree_store_remove(GTK_TREE_STORE(priv->model), &iter);
    priv->folders = g_list_remove(priv->folders, &folderdata);
    monocle_thumbpane_next_folder(self);
}

void
monocle_thumbpane_remove_current (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeModel *filter;
    GList *rows;
    GList *row_refs = NULL;
    
    filter = gtk_tree_view_get_model(priv->treeview);
    rows = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), &filter);
    
    /* convert the list of treepaths into row references */
    /* avoids WOAH NELLY when we remove */
    while (rows != NULL) {
        row_refs = g_list_prepend(row_refs, gtk_tree_row_reference_new(priv->model, 
                                  gtk_tree_model_filter_convert_path_to_child_path(GTK_TREE_MODEL_FILTER(filter), (GtkTreePath *)rows->data))
                   );
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
    MonocleThumbpaneFolder *folderdata;
    GList *elem;
    GtkTreeIter first;
    GtkTreeModel *filter;
    
    if (g_list_length(priv->folders) <= 1)
        return;

    elem = g_list_nth(priv->folders, priv->curfolder);
    if (elem != NULL)
        elem = g_list_next(elem);
    if (elem == NULL)
        elem = g_list_first(priv->folders);
    if (elem == NULL)
        return;

    priv->curfolder = g_list_position(priv->folders, elem);

    folderdata = (MonocleThumbpaneFolder *)elem->data;
    filter = gtk_tree_model_filter_new(priv->model, gtk_tree_row_reference_get_path(folderdata->rowref));
    gtk_tree_model_get_iter_first(filter, &first);
    
    gtk_tree_view_set_model(priv->treeview, filter);
    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), &first);

    g_object_unref(filter);
}

void
monocle_thumbpane_prev_folder (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    MonocleThumbpaneFolder *folderdata;
    GList *elem;
    GtkTreeIter first;
    GtkTreeModel *filter;

    if (g_list_length(priv->folders) <= 1)
        return;

    elem = g_list_nth(priv->folders, priv->curfolder);
    if (elem != NULL)
        elem = g_list_previous(elem);
    if (elem == NULL)
        elem = g_list_last(priv->folders);
    if (elem == NULL)
        return;
    
    priv->curfolder = g_list_position(priv->folders, elem);

    folderdata = (MonocleThumbpaneFolder *)elem->data;
    filter = gtk_tree_model_filter_new(priv->model, gtk_tree_row_reference_get_path(folderdata->rowref));
    gtk_tree_model_get_iter_first(filter, &first);
    
    gtk_tree_view_set_model(priv->treeview, filter);
    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), &first);

    g_object_unref(filter);   
}

void
monocle_thumbpane_select_folder (MonocleThumbpane *self, gchar *folder) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    MonocleThumbpaneFolder folderdata;
    gint folderidx;
    GtkTreeIter first;
    GtkTreeModel *filter;

    folderidx = get_folder_in_model(self, &folderdata, folder);
    if (folderidx >= 0) {
        priv->curfolder = folderidx;

        filter = gtk_tree_model_filter_new(priv->model, gtk_tree_row_reference_get_path(folderdata.rowref));
        gtk_tree_model_get_iter_first(filter, &first);

        gtk_tree_view_set_model(priv->treeview, filter);
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), &first);

        g_object_unref(filter);
    }
}

/* These seem redundant, maybe have an arg with some exposed ENUMS or something */
/* FIXME: make these convenience functions that call something else */

void
monocle_thumbpane_sort_by_name (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(priv->model);
    gtk_tree_sortable_set_sort_column_id(sortable, SORT_NAME, priv->sort_order);
}

void
monocle_thumbpane_sort_by_date (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(priv->model);
    gtk_tree_sortable_set_sort_column_id(sortable, SORT_DATE, priv->sort_order);
}

void
monocle_thumbpane_sort_by_size (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(priv->model);
    gtk_tree_sortable_set_sort_column_id(sortable, SORT_SIZE, priv->sort_order);
}

/* These are ugly, they look pretty but they're ugly */
void
monocle_thumbpane_sort_order_ascending (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(priv->model);
    gint sort_type;

    priv->sort_order = GTK_SORT_ASCENDING;
    gtk_tree_sortable_get_sort_column_id(sortable, &sort_type, NULL);
    gtk_tree_sortable_set_sort_column_id(sortable, sort_type, priv->sort_order);
}

void
monocle_thumbpane_sort_order_descending (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(priv->model);
    gint sort_type;
    
    priv->sort_order = GTK_SORT_DESCENDING;
    gtk_tree_sortable_get_sort_column_id(sortable, &sort_type, NULL);
    gtk_tree_sortable_set_sort_column_id(sortable, sort_type, priv->sort_order);
}

/* takes a folder name as an argument
 * finds a MonocleThumbpaneFolder pointing to the node in the treemodel
 * returns -1 if it was found, index of the folder otherwise
 */
static gint
get_folder_in_model (MonocleThumbpane *self, MonocleThumbpaneFolder *out, gchar *folder) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);

    GList *elem;

    elem = g_list_first(priv->folders);
    while (elem != NULL) {
        MonocleThumbpaneFolder *folderdata = (MonocleThumbpaneFolder *)elem->data;
        if (!strcmp(folderdata->name, folder)) {
                memcpy(out, folderdata, sizeof(MonocleThumbpaneFolder));
                return g_list_position(priv->folders, elem);
        }
        elem = g_list_next(elem);
    }

    return -1;
}

/* to be used in conjunction with the above function
 * creates a new row in the treemodel and sets out to a MonocleThumbpaneFolder
 */
static void
create_folder_in_model (MonocleThumbpane *self, MonocleThumbpaneFolder *out, gchar *folder) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    MonocleThumbpaneFolder *folderdata;
    GtkTreeIter iter;

    gtk_tree_store_insert_with_values(GTK_TREE_STORE(priv->model), &iter, NULL,
                                      gtk_tree_model_iter_n_children(priv->model, NULL),
                                      COL_FILEPATH, folder,
                                      COL_THUMBNAIL, NULL,
                                      COL_ISDIR, TRUE,
                                      -1);
    folderdata = g_malloc(sizeof(MonocleThumbpaneFolder));
    folderdata->name = g_strdup(folder);
    folderdata->rowref = gtk_tree_row_reference_new(priv->model, gtk_tree_model_get_path(priv->model, &iter));
    priv->folders = g_list_append(priv->folders, folderdata);

    /* I feel this is a little less gross than declaring folderdata static */
    memcpy(out,folderdata, sizeof(MonocleThumbpaneFolder));
}

void
monocle_thumbpane_set_num_threads (MonocleThumbpane *self, gint num_threads) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    priv->num_threads = num_threads > 0 ? num_threads : 1;
    return;
}

gint
monocle_thumbpane_get_num_threads (MonocleThumbpane *self) {
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    return priv->num_threads;
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
            gtk_tree_model_get(model, &iter, COL_FILEPATH, &filename, -1);
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

    /* remove the folder that this iter should be pointing to */
    gtk_tree_model_get(model, iter, COL_ISDIR, &is_folder, -1);
    if (is_folder) {
        if (gtk_tree_model_iter_n_children(model, iter) == 0) {
            gtk_tree_model_get(model, iter, COL_FILEPATH, &folder, -1);
            monocle_thumbpane_remove_folder(self, folder);
        }
    }
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
    
    gtk_tree_model_get(model, a, COL_FILEPATH, &filea, -1);
    gtk_tree_model_get(model, b, COL_FILEPATH, &fileb, -1);

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

/* TODO: make sure this is actually properly done */
static void
thumbnail_thread (GtkTreeRowReference *rowref) {
    GtkTreeModel *model;
    GtkTreePath *path;
    GtkTreeIter row;
    
    gchar *filename;
    GdkPixbuf *thumb;
    
    gdk_threads_enter();
    path = gtk_tree_row_reference_get_path(rowref);
    model = gtk_tree_row_reference_get_model(rowref);
    gtk_tree_row_reference_free(rowref);
    gdk_threads_leave();
    
    if (path == NULL)
        return;

    gdk_threads_enter();
    gtk_tree_model_get_iter(model, &row, path);
    gtk_tree_model_get(model, &row, COL_FILEPATH, &filename, -1);
    gdk_threads_leave();

    if (filename == NULL)
        return;

    thumb = generate_thumbnail(filename);
    if (thumb != NULL) {
        gdk_threads_enter();
        gtk_tree_store_set(GTK_TREE_STORE(model), &row, COL_THUMBNAIL, thumb, -1);
        gdk_threads_leave();
        g_object_unref(thumb);
    }

    g_free(filename);
}

/* returns a NULL if one can't be made */
static GdkPixbuf
*generate_thumbnail (gchar *filename) {
#ifdef WIN32
    return gdk_pixbuf_new_from_file_at_size(filename, 128, -1, NULL);
#else
    gchar *uri = encode_file_uri(filename);
    gchar *md5uri = md5sum(uri);
    gchar *homedir = getenv("HOME"); /* put this elsewhere, no sense calling it everytime we want a thumbnail */
    gchar *file;

    GdkPixbuf *thumb;

    file = g_malloc(strlen(homedir) + strlen(md5uri) + 25);
    sprintf(file, "%s/.thumbnails/normal/%s.png", homedir, md5uri);
    if ((thumb = gdk_pixbuf_new_from_file(file, NULL)) == NULL) {
        thumb = gdk_pixbuf_new_from_file_at_size(filename, 128, -1, NULL);
    }
    
    g_free(uri);
    g_free(file);
    return thumb;
#endif
}

/* md5hashes a string */
static gchar
*md5sum (gchar *str) {
    md5_state_t state;
    md5_byte_t digest[16];
    static char hexout[33]; /* md5 + nul */
    int di;

    md5_init(&state);
    md5_append(&state, (const md5_byte_t *)str, strlen(str));
    md5_finish(&state, digest);
    for (di = 0; di < 16; ++di)
        sprintf(hexout + di * 2, "%02x", digest[di]);
    
    return hexout;
}


/* takes a filename as argument, returns the uri for the filename, uri encoded as such: file://FILENAME%20WITH%20SPACES */
/* free the returned string after use */
static gchar
*encode_file_uri (gchar *str) {
    static const gchar hex[] = "0123456789abcdef";
    gchar *pstr = str;
    gchar *buf = g_malloc(strlen(str) * 3 + 8);
    gchar *pbuf = buf;

    gchar *uri;

    while (*pstr) {
        if (isalnum(*pstr) || *pstr == '.' || *pstr == '-' || *pstr == '_' || *pstr == '/') {
            *pbuf++ = *pstr;
        } else {
            *pbuf++ = '%';
            /*high byte*/
            *pbuf++ = hex[(*pstr >> 4) & 0xf];
            /*low byte*/
            *pbuf++ = hex[(*pstr & 0xf) & 0xf];
        }
        pstr++;
    }
    *pbuf = '\0';

    /* Ugly to do this malloc, keeps from using extra space that isn't needed previously and keeps code neat but isn't really necessary */
    uri = g_malloc(strlen(buf) + 8);
    sprintf(uri, "file://%s", buf);
    g_free(buf);

    return uri;
}
