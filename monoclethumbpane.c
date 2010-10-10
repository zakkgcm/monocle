/* MonocleThumbPane implementation file
 * goals: asynchronous, minimal mem usage
 * issues: keeping thumbnails in memory vs hard drive
 */

#include <string.h>
#include <ctype.h>
#include "monoclethumbpane.h"
#include "utils/md5.h"

#define MONOCLE_THUMBPANE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), MONOCLE_TYPE_THUMBPANE, MonocleThumbpanePrivate))

typedef struct _MonocleThumbpanePrivate {
    GtkTreeView *treeview;
} MonocleThumbpanePrivate;

/* Thumbpane keeps a list of the loaded files handy, nothing else really has a use for such a list so there's no need to keep it outside of this widget */

/* TODO: don't inherit scrolled window or something */
G_DEFINE_TYPE(MonocleThumbpane, monocle_thumbpane, GTK_TYPE_SCROLLED_WINDOW)

enum {
    COL_FILENAME = 0,
    COL_THUMBNAIL,
    NUM_COLS
};

enum {
    CHANGED_SIGNAL,
    LAST_SIGNAL
};

static void monocle_thumbpane_size_allocate (GtkWidget *widget, GtkAllocation *allocation);
static gboolean cb_row_selected (GtkTreeSelection *selection, GtkTreeModel *model, GtkTreePath *path, gboolean curpath, gpointer user_data);
static GdkPixbuf *generate_thumbnail (gchar *filename);
static gchar *md5sum (gchar *str);
static gchar *encode_file_uri (gchar *str);

static guint monocle_thumbpane_signals[LAST_SIGNAL] = { 0 };

static void
monocle_thumbpane_init (MonocleThumbpane *self){
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkWidget         *treeview;
    GtkListStore        *list;
    GtkTreeViewColumn   *col;
    GtkTreeSelection    *sel;
    GtkCellRenderer     *thumbnailer;

    priv->treeview = NULL;

    list        = gtk_list_store_new( NUM_COLS, G_TYPE_STRING, GDK_TYPE_PIXBUF );
    treeview    = gtk_tree_view_new();
    col         = gtk_tree_view_column_new();
    sel         = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    thumbnailer = gtk_cell_renderer_pixbuf_new();

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);

    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
    gtk_tree_view_column_pack_start(col, thumbnailer, TRUE);

    gtk_tree_view_column_add_attribute(col, thumbnailer, "pixbuf", COL_THUMBNAIL);

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(list));
    g_object_unref(list);

    gtk_tree_selection_set_mode(sel, GTK_SELECTION_BROWSE);
    gtk_tree_selection_set_select_function(sel, cb_row_selected, self, NULL);

    priv->treeview = GTK_TREE_VIEW(treeview);
    
    gtk_container_add(GTK_CONTAINER(self), GTK_WIDGET(priv->treeview)); /*(monocle:10764): Gtk-CRITICAL **: gtk_range_get_adjustment: assertion `GTK_IS_RANGE (range)' failed*/
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
}


static void
monocle_thumbpane_class_init (MonocleThumbpaneClass *klass){
    /* GObjectClass *g_class = G_OBJECT_CLASS(klass); */
    GtkWidgetClass *w_class = GTK_WIDGET_CLASS(klass);

    g_type_class_add_private(klass, sizeof(MonocleThumbpanePrivate));
    /*w_class->size_allocate = monocle_thumbpane_size_allocate;*/

    monocle_thumbpane_signals[CHANGED_SIGNAL] =
            g_signal_new( "image-changed", G_TYPE_FROM_CLASS(klass),
                      G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET(MonocleThumbpaneClass, monocle_thumbpane),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE, 
                      1, G_TYPE_STRING );
}

void
monocle_thumbpane_add_image (MonocleThumbpane *self, gchar *filename){
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GdkPixbuf *thumb;
    GtkListStore *list;
    GtkTreeIter row;

    thumb = generate_thumbnail(filename);

    list = GTK_LIST_STORE(gtk_tree_view_get_model(priv->treeview));
    gtk_list_store_append(list, &row);
    gtk_list_store_set(list, &row, COL_FILENAME, filename, -1);
    gtk_list_store_set(list, &row, COL_THUMBNAIL, thumb, -1);
    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), &row);

    g_object_unref(thumb);
}

/* Add a whole bunch of images (or just two whichever) */
void
monocle_thumbpane_add_many (MonocleThumbpane *self, GSList *filenames){
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GdkPixbuf *thumb;
    GtkListStore *list;
    GtkTreeIter row;

    list = GTK_LIST_STORE(gtk_tree_view_get_model(priv->treeview));
    g_object_ref(list);
    gtk_tree_view_set_model(priv->treeview, NULL);

    do {
        thumb = generate_thumbnail((gchar *)filenames->data);
        gtk_list_store_append(list, &row);
        gtk_list_store_set(list, &row, COL_FILENAME, (gchar *)filenames->data, -1);
        gtk_list_store_set(list, &row, COL_THUMBNAIL, thumb, -1);
        g_object_unref(thumb);
    } while ((filenames = g_slist_next(filenames)) != NULL);
    
    gtk_tree_view_set_model(priv->treeview, GTK_TREE_MODEL(list));
    /* TODO: make this select the first item in the list */
    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview)), &row);
    g_object_unref(list);
}

/* Add a whole bunch of images (or just two whichever) from a directory */
/* it's cool because it can be easily made recursive */
void
monocle_thumbpane_add_folder (MonocleThumbpane *self, gchar *folder, gboolean recursive){
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GDir *folder_tree;
    GSList *filenames;
    gchar *filename, *filepath;
    
    filenames = NULL;

    if((folder_tree = g_dir_open(folder, 0, NULL)) == NULL)
        return;

    while ((filename = g_dir_read_name(folder_tree)) != NULL){
        /* do not modify or free the filename, copy it in a sensible manner please */
        /* we don't modify it in any case but fix it anyway */
        /* some check for an image file here
         * want to avoid just checking file extension since it may not be true*/
        /* patch together the full path of the file, 2 is a backslash + nul */
        filepath = g_malloc(strlen(folder) + strlen(filename) + 2);
        sprintf(filepath, "%s/%s", folder, filename);
        if(g_file_test(filepath, G_FILE_TEST_IS_REGULAR)){
            filenames = g_slist_prepend(filenames, filepath); /* prepend + reverse is quicker */
        }
    }
    filenames = g_slist_reverse(filenames);

    g_dir_close(folder_tree);
    monocle_thumbpane_add_many(self, filenames);

    g_slist_free(filenames);
}

static void 
monocle_thumbpane_size_allocate (GtkWidget *widget, GtkAllocation *allocation){
    GtkWidget *child;
    GtkAllocation child_allocation;
    
    gtk_widget_set_allocation (widget, allocation);
    
    child = gtk_bin_get_child (GTK_BIN (widget));
    if (child && gtk_widget_get_visible (child)){
        child_allocation.x = allocation->x;
        child_allocation.y = allocation->y;
        child_allocation.width = allocation->width;
        child_allocation.height = allocation->height;
        gtk_widget_size_allocate (child, &child_allocation);
    }
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

    if(!curpath){
        /* handle this error */
        if(gtk_tree_model_get_iter(model, &iter, path)){
            gtk_tree_model_get(model, &iter, COL_FILENAME, &filename, -1);
            /* I think this is ugly, handler should get the filename itself possibly */
            g_signal_emit(G_OBJECT(self), monocle_thumbpane_signals[CHANGED_SIGNAL], 0, filename);
        }
    }
    
    return TRUE;

}

/* TODO: move this to another thread or idle loop */
static GdkPixbuf
*generate_thumbnail (gchar *filename){
    gchar *uri = encode_file_uri(filename);
    gchar *md5uri = md5sum(uri);
    gchar *homedir = getenv("HOME"); /* put this elsewhere, no sense calling it everytime we want a thumbnail */
    gchar *file;

    GdkPixbuf *thumb;

    file = g_malloc(strlen(homedir) + strlen(md5uri) + 25);
    sprintf(file, "%s/.thumbnails/normal/%s.png", homedir, md5uri);
    if((thumb = gdk_pixbuf_new_from_file(file, NULL)) == NULL){
        /* add thumbnail to generation queue here */
        /* who was thumbnail? */
        thumb = gdk_pixbuf_new_from_file("./Itisamystery.gif", NULL);
    }
    
    g_free(uri);
    g_free(file);
    return thumb;
}

/* md5hashes a string */
static gchar
*md5sum (gchar *str){
    md5_state_t state;
    md5_byte_t digest[16];
    char hexout[33]; /* md5 + nul */
    int di;

    md5_init(&state);
    md5_append(&state, (const md5_byte_t *)str, strlen(str));
    md5_finish(&state, digest);
    for (di = 0; di < 16; ++di)
        sprintf(hexout + di * 2, "%02x", digest[di]);
    
    return hexout; /* monoclethumbpane.c:182:5: warning: function returns address of local variable */
}


/* takes a filename as argument, returns the uri for the filename, uri encoded as such: file://FILENAME%20WITH%20SPACES */
/* free the returned string after use */
static gchar
*encode_file_uri (gchar *str){
    static const gchar hex[] = "0123456789abcdef";
    gchar *pstr = str;
    gchar *buf = g_malloc(strlen(str) * 3 + 8);
    gchar *pbuf = buf;

    gchar *uri;

    while (*pstr){
        if(isalnum(*pstr) || *pstr == '.' || *pstr == '-' || *pstr == '_' || *pstr == '/'){
            *pbuf++ = *pstr;
        }else{
            *pbuf++ = '%';
            /*high byte*/
            *pbuf++ = hex[(*pstr >> 4) & 0xf];
            /*low byte*/
            *pbuf++ = hex[(*pstr & 0xf) & 0xf];
        }
        *pstr++; /* monoclethumbpane.c:130:9: warning: value computed is not used < wat */
    }
    *pbuf = '\0';

    /* Ugly to do this malloc, keeps from using extra that isn't needed previously and keeps code neat but isn't really necessary */
    uri = g_malloc(strlen(buf) + 8);
    sprintf(uri, "file://%s", buf);
    g_free(buf);

    return uri;
}
