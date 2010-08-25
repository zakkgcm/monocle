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

G_DEFINE_TYPE(MonocleThumbpane, monocle_thumbpane, GTK_TYPE_BIN)

enum {
    COL_FILENAME = 0,
    COL_THUMBNAIL,
    NUM_COLS
};

static void monocle_thumbpane_size_allocate (GtkWidget *widget, GtkAllocation *allocation);
static gchar *encode_file_uri (gchar *str);

static void
monocle_thumbpane_init (MonocleThumbpane *self){
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    GtkWidget         *treeview;
    GtkListStore        *list;
    GtkTreeViewColumn   *col;
    GtkCellRenderer     *thumbnailer;

    priv->treeview = NULL;

    list        = gtk_list_store_new( NUM_COLS, G_TYPE_STRING, GDK_TYPE_PIXBUF );
    treeview    = gtk_tree_view_new();
    col         = gtk_tree_view_column_new();
    thumbnailer = gtk_cell_renderer_pixbuf_new();

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);

    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
    gtk_tree_view_column_pack_start(col, thumbnailer, TRUE);

    gtk_tree_view_column_add_attribute(col, thumbnailer, "pixbuf", COL_THUMBNAIL);

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(list));
    g_object_unref(list);

    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)), GTK_SELECTION_BROWSE);
    
    priv->treeview = GTK_TREE_VIEW(treeview);

    gtk_container_add(GTK_CONTAINER(self), GTK_WIDGET(priv->treeview));
}


static void
monocle_thumbpane_class_init (MonocleThumbpaneClass *klass){
    /* GObjectClass *g_class = G_OBJECT_CLASS(klass); */
    GtkWidgetClass *w_class = GTK_WIDGET_CLASS(klass);

    g_type_class_add_private(klass, sizeof(MonocleThumbpanePrivate));

    w_class->size_allocate = monocle_thumbpane_size_allocate;
}

void
monocle_thumbpane_add_image (MonocleThumbpane *self, gchar *filename){
    MonocleThumbpanePrivate *priv = MONOCLE_THUMBPANE_GET_PRIVATE(self);
    gchar *uri = encode_file_uri(filename);
    GdkPixbuf *thumb;
    GtkListStore *list;
    GtkTreeIter row;

    /* TODO: Check through ~/.thumbnails and find an appropriate thumbnail for the image */
    /* TODO: md5 "uri" and look for a thumbnail in the thumbnails dir */

    thumb = gdk_pixbuf_new_from_file("./Itisamystery.gif", NULL);

    list = GTK_LIST_STORE(gtk_tree_view_get_model(priv->treeview));
    gtk_list_store_append(list, &row);
    gtk_list_store_set(list, &row, COL_THUMBNAIL, thumb, -1);
    
    g_object_unref(thumb);
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

/* takes a filename as argument, returns the uri for the filename, uri encoded as such: file://FILENAME */
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
