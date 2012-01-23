#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "monoclethumblist.h"
#include "utils/md5.h"

/* BOILERPLATE */
/* FIXME: clean up iter error handling into a func ala gtk sources *
  * includes stamp = 0 stuff */

static void monocle_thumblist_init            (MonocleThumblist      *pkg_tree);
static void monocle_thumblist_class_init      (MonocleThumblistClass *klass);
static void monocle_thumblist_tree_model_init (GtkTreeModelIface     *iface);
static void monocle_thumblist_finalize        (GObject               *object);

static GtkTreeModelFlags monocle_thumblist_get_flags  (GtkTreeModel      *model);

static gint     monocle_thumblist_get_n_columns   (GtkTreeModel *model);
static GType    monocle_thumblist_get_column_type (GtkTreeModel *model,
                                                   gint         index);

static gboolean     monocle_thumblist_get_iter   (GtkTreeModel   *model,
                                                  GtkTreeIter    *iter,
                                                  GtkTreePath    *path);

static GtkTreePath *monocle_thumblist_get_path   (GtkTreeModel   *model,
                                                  GtkTreeIter    *iter);

static void         monocle_thumblist_get_value  (GtkTreeModel   *model,
                                                  GtkTreeIter    *iter,
                                                  gint            column,
                                                  GValue         *value);

static gboolean monocle_thumblist_iter_next       (GtkTreeModel   *model,
                                                   GtkTreeIter    *iter);

static gboolean monocle_thumblist_iter_children   (GtkTreeModel   *model,
                                                   GtkTreeIter    *iter,
                                                   GtkTreeIter    *parent);

static gboolean monocle_thumblist_iter_has_child  (GtkTreeModel   *model,
                                                   GtkTreeIter    *iter);

static gint     monocle_thumblist_iter_n_children (GtkTreeModel   *model,
                                                   GtkTreeIter    *iter);

static gboolean monocle_thumblist_iter_nth_child  (GtkTreeModel   *model,
                                                   GtkTreeIter    *iter,
                                                   GtkTreeIter    *parent,
                                                   gint            n);

static gboolean monocle_thumblist_iter_parent     (GtkTreeModel   *model,
                                                   GtkTreeIter    *iter,
                                                   GtkTreeIter    *child);
static GObjectClass *parent_class = NULL;

static void monocle_file_free (MonocleFile *file);

/* thumbnailing prototypes */ 
static void      thumbnail_thread_func    (MonocleThumblist *monocle_thumblist);
static GdkPixbuf *generate_thumbnail      (gchar *filename);

static void monocle_thumblist_thumbqueue_push (MonocleThumblist *monocle_thumblist, MonocleFile *file);
static void monocle_thumblist_thumbqueue_remove (MonocleThumblist *monocle_thumblist, gchar *file);
static void monocle_thumblist_thumbqueue_remove_many (MonocleThumblist *monocle_thumblist, GSList *files);

static gchar *md5sum          (gchar *str);
static gchar *encode_file_uri (gchar *str);

static GdkPixbuf *default_thumbnail = NULL;

/* GObject and GtkTreeModel implementation */

GType
monocle_thumblist_get_type (void)
{
    static GType monocle_thumblist_type = 0;

    if (monocle_thumblist_type == 0) {
        static const GTypeInfo monocle_thumblist_info =
        {
            sizeof (MonocleThumblistClass),
            NULL,                                           /* base_init */
            NULL,                                           /* base_finalize */
            (GClassInitFunc) monocle_thumblist_class_init,
            NULL,                                           /* class_finalize */
            NULL,                                           /* class_data */
            sizeof (MonocleThumblist),
            0,                                              /* n_preallocs */
            (GInstanceInitFunc) monocle_thumblist_init
        };
        static const GInterfaceInfo tree_model_info = 
        {
            (GInterfaceInitFunc) monocle_thumblist_tree_model_init,
            NULL,
            NULL
        };

        monocle_thumblist_type = g_type_register_static (G_TYPE_OBJECT, "MonocleThumblist",
                                                         &monocle_thumblist_info, (GTypeFlags)0);
        g_type_add_interface_static (monocle_thumblist_type, GTK_TYPE_TREE_MODEL, &tree_model_info);
    }

    return monocle_thumblist_type;
}

static void
monocle_thumblist_class_init (MonocleThumblistClass *klass)
{
    GObjectClass *object_class;

    parent_class = (GObjectClass *) g_type_class_peek_parent(klass);
    object_class = (GObjectClass *) klass;

    object_class->finalize = monocle_thumblist_finalize;

    default_thumbnail = g_file_test("./Itisamystery.gif", G_FILE_TEST_IS_REGULAR)
                                ? gdk_pixbuf_new_from_file("./Itisamystery.gif", NULL)
                                : gdk_pixbuf_new_from_file("/usr/share/monocle/Itisamystery.gif", NULL);
}

static void
monocle_thumblist_tree_model_init (GtkTreeModelIface *iface)
{
    iface->get_flags       = monocle_thumblist_get_flags;
    iface->get_n_columns   = monocle_thumblist_get_n_columns;
    iface->get_column_type = monocle_thumblist_get_column_type;
    iface->get_iter        = monocle_thumblist_get_iter;
    iface->get_path        = monocle_thumblist_get_path;
    iface->get_value       = monocle_thumblist_get_value;
    iface->iter_next       = monocle_thumblist_iter_next;
    iface->iter_children   = monocle_thumblist_iter_children;
    iface->iter_has_child  = monocle_thumblist_iter_has_child;
    iface->iter_n_children = monocle_thumblist_iter_n_children;
    iface->iter_nth_child  = monocle_thumblist_iter_nth_child;
    iface->iter_parent     = monocle_thumblist_iter_parent;
}

static void
monocle_thumblist_init (MonocleThumblist *monocle_thumblist)
{
    monocle_thumblist->num_rows = 0;
    monocle_thumblist->stamp = g_random_int();
    
    monocle_thumblist->thumb_mutex = g_mutex_new();
    g_static_rw_lock_init(&monocle_thumblist->thumb_rwlock); 
    monocle_thumblist->thumb_cond = g_cond_new();
}

MonocleThumblist *
monocle_thumblist_new ()
{
    return g_object_new (MONOCLE_TYPE_THUMBLIST, NULL);
}

static void
monocle_thumblist_finalize (GObject *object)
{
    MonocleThumblist *monocle_thumblist = MONOCLE_THUMBLIST(object);

    /* free up internal data and stuff here*/

    /* finalize the parent */
    (* parent_class->finalize) (object);
}

static GtkTreeModelFlags
monocle_thumblist_get_flags (GtkTreeModel *model)
{
    return (GTK_TREE_MODEL_LIST_ONLY | GTK_TREE_MODEL_ITERS_PERSIST);
}

static gint
monocle_thumblist_get_n_columns (GtkTreeModel *model)
{
    return 2;
}

static GType
monocle_thumblist_get_column_type (GtkTreeModel *model, gint index)
{
    g_return_val_if_fail (index < MONOCLE_THUMBLIST_N_COLS && index >= 0, G_TYPE_INVALID);

    switch (index) {
        case MONOCLE_THUMBLIST_COL_FILENAME:
            return G_TYPE_STRING;
            break;

        case MONOCLE_THUMBLIST_COL_THUMBNAIL:
            return GDK_TYPE_PIXBUF;
            break;
        
        default:
            return G_TYPE_INVALID; /* this will never be reached ever */
            break;
    }
}

/* TODO(?): wrap these functions with mutexes */

static gboolean
monocle_thumblist_get_iter (GtkTreeModel *model, GtkTreeIter *iter, GtkTreePath *path)
{
    MonocleThumblist *monocle_thumblist = MONOCLE_THUMBLIST(model);
    gint n;

    g_assert(gtk_tree_path_get_depth(path) == 1); /* lists do not have leaves */
    
    n = gtk_tree_path_get_indices(path)[0];
    if (monocle_thumblist->current_folder == NULL ||
        n >= g_list_length(monocle_thumblist->current_folder->files) || n < 0) {
        
        iter->stamp = 0;
        return FALSE;
    }
    
    iter->stamp = monocle_thumblist->stamp;
    iter->user_data = g_list_nth(monocle_thumblist->current_folder->files, n)->data;

    return TRUE;
}

static GtkTreePath *
monocle_thumblist_get_path (GtkTreeModel *model, GtkTreeIter *iter)
{
    MonocleThumblist *monocle_thumblist = MONOCLE_THUMBLIST(model);
    MonocleFile *file;
    GList *file_elem;

    GtkTreePath *path;

    g_return_val_if_fail (iter->stamp == monocle_thumblist->stamp, NULL);

    file = (MonocleFile *)iter->user_data;
    file_elem = g_list_find(monocle_thumblist->current_folder->files, file);

    path = gtk_tree_path_new();
    gtk_tree_path_append_index(path, g_list_position(monocle_thumblist->current_folder->files, file_elem));

    return path;
}

static void
monocle_thumblist_get_value (GtkTreeModel *model, GtkTreeIter *iter,
                             gint column, GValue *value)
{
    MonocleThumblist *monocle_thumblist = MONOCLE_THUMBLIST(model);
    MonocleFile *file;

    g_return_if_fail(iter != NULL);
    g_return_if_fail(iter->stamp == monocle_thumblist->stamp);
    g_return_if_fail(column < MONOCLE_THUMBLIST_N_COLS);

    g_value_init (value, monocle_thumblist_get_column_type(GTK_TREE_MODEL(monocle_thumblist), column));
    
    file = (MonocleFile *)iter->user_data;
    
    g_static_rw_lock_reader_lock(&monocle_thumblist->thumb_rwlock);
    switch (column) {
        case MONOCLE_THUMBLIST_COL_FILENAME:
            g_value_set_string(value, file->name);
            break;

        case MONOCLE_THUMBLIST_COL_THUMBNAIL:
            g_value_set_object(value, file->thumbnail);
            break;
    }
    g_static_rw_lock_reader_unlock(&monocle_thumblist->thumb_rwlock);
}

static gboolean
monocle_thumblist_iter_next (GtkTreeModel *model, GtkTreeIter *iter)
{
    MonocleThumblist *monocle_thumblist = MONOCLE_THUMBLIST(model);
    MonocleFile *file;
    GList *file_elem, *next_elem;

    g_return_val_if_fail (iter->stamp == monocle_thumblist->stamp, FALSE);

    file = (MonocleFile *)iter->user_data;
    file_elem = g_list_find(monocle_thumblist->current_folder->files, file);

    if ((next_elem = g_list_next(file_elem)) != NULL) {
        iter->user_data = next_elem->data;
        return TRUE;
    }

    /* couldn't get next element */
    iter->stamp = 0;
    return FALSE;
}

static gboolean
monocle_thumblist_iter_children (GtkTreeModel *model,
                                 GtkTreeIter *iter, GtkTreeIter *parent)
{
    MonocleThumblist *monocle_thumblist = MONOCLE_THUMBLIST(model);

    /* lists have no children
     * only handle the special NULL case (return first node */
    if (parent == NULL && g_list_length(monocle_thumblist->current_folder->files) > 0) {
        iter->stamp = monocle_thumblist->stamp;
        iter->user_data = g_list_first(monocle_thumblist->current_folder->files)->data;
        return TRUE;
    }

    iter->stamp = 0;
    return FALSE;
}

static gboolean
monocle_thumblist_iter_has_child (GtkTreeModel *model, GtkTreeIter *iter)
{
    return FALSE;
}

static gint
monocle_thumblist_iter_n_children (GtkTreeModel *model, GtkTreeIter *iter)
{
    MonocleThumblist *monocle_thumblist = MONOCLE_THUMBLIST(model);
    
    if (iter == NULL) /* return # of top level nodes */
        return g_list_length(monocle_thumblist->current_folder->files);
        
    g_return_val_if_fail (iter->stamp != monocle_thumblist->stamp, -1);
    return 0;
}

static gboolean
monocle_thumblist_iter_nth_child (GtkTreeModel *model, 
                                  GtkTreeIter *iter, GtkTreeIter *parent,
                                  gint n)

{
    MonocleThumblist *monocle_thumblist = MONOCLE_THUMBLIST(model);

    if (parent)
        return FALSE;

    iter->stamp = monocle_thumblist->stamp;
    iter->user_data = g_list_nth(monocle_thumblist->current_folder->files, n)->data;
    return TRUE;
}

static gboolean
monocle_thumblist_iter_parent (GtkTreeModel *model,
                               GtkTreeIter *iter, GtkTreeIter *child)
{
    iter->stamp = 0;
    return FALSE;
}

/* Thumblist specific implementation */

/* behaves as gtk_list_store_remove does 
 * with the caveat that iter need to be a row in the current folder */
gboolean
monocle_thumblist_remove (MonocleThumblist *monocle_thumblist, 
                          GtkTreeIter *iter)
{
    GtkTreePath *path;
    GtkTreeIter next_row;
    gboolean valid;

    g_return_val_if_fail(iter != NULL, FALSE);
    g_return_val_if_fail(iter->stamp == monocle_thumblist->stamp, FALSE);

    g_mutex_lock(monocle_thumblist->thumb_mutex);
    g_static_rw_lock_writer_lock(&monocle_thumblist->thumb_rwlock);

    /* gather data about next rows and paths before removing */
    next_row = *iter;

    valid = monocle_thumblist_iter_next(GTK_TREE_MODEL(monocle_thumblist), &next_row);
    path = monocle_thumblist_get_path(GTK_TREE_MODEL(monocle_thumblist), iter);

    monocle_thumblist->current_folder->files = g_list_remove(
                                monocle_thumblist->current_folder->files,
                                (MonocleFile *)iter->user_data);

    g_static_rw_lock_writer_unlock(&monocle_thumblist->thumb_rwlock);
    g_mutex_unlock(monocle_thumblist->thumb_mutex);

    gtk_tree_model_row_deleted(GTK_TREE_MODEL(monocle_thumblist), path);
    gtk_tree_path_free(path);
    
    /* if there was no next row, these will be 0, NULL anyway */
    iter->stamp = next_row.stamp;
    iter->user_data = next_row.user_data;

    return valid;
}

/* removes all entries belonging to the current folder
 * sets out to the next available folder or NULL */
/* FIXME: clean this up */
gboolean
monocle_thumblist_remove_current_folder (MonocleThumblist *monocle_thumblist, MonocleFolder **out)
{
    MonocleFolder *folder;
    GList *elem, *next_elem, *file_elem;
    GSList *remove_list = NULL;
    gboolean valid = FALSE;
    gint length;

    GtkTreePath *path;

    if (!monocle_thumblist->current_folder || !monocle_thumblist->folders)
        return FALSE; /* no folders/current folder */

    g_mutex_lock(monocle_thumblist->thumb_mutex);
    g_static_rw_lock_writer_lock(&monocle_thumblist->thumb_rwlock);

    /* the next step will change current_folder */
    folder = monocle_thumblist->current_folder;

    /* notify deletion the visible rows
     * via selecting a NULL folder */
    monocle_thumblist_select_folder(monocle_thumblist, NULL);

    /* position of the current folder in the folder list */
    elem = g_list_find(monocle_thumblist->folders, folder);

    /* point out to the next folder */
    next_elem = g_list_next(elem);
    if (next_elem) {
        valid = TRUE;
        *out = (MonocleFolder *)next_elem->data;
    }

    /* remove the file entries from the thumbnail queue */
    file_elem = g_list_first(folder->files);
    while (file_elem != NULL) {
        remove_list = g_slist_append(remove_list, ((MonocleFile *)file_elem->data)->name);
        file_elem = g_list_next(file_elem);
    }

    monocle_thumblist_thumbqueue_remove_many(monocle_thumblist, remove_list);

    /* actually free up the current folder's stuff */
    file_elem = g_list_first(folder->files);
    while (file_elem != NULL) {
        monocle_file_free(file_elem->data);
        file_elem = g_list_next(file_elem);
    }

    g_free(elem->data); /* MonocleFolder */
    monocle_thumblist->folders = g_list_delete_link(monocle_thumblist->folders, elem);
    
    g_static_rw_lock_writer_unlock(&monocle_thumblist->thumb_rwlock);
    g_mutex_unlock(monocle_thumblist->thumb_mutex);
    
    return valid;
}

/* removes everything, restoring the list to "just initialized" state */
void
monocle_thumblist_clear (MonocleThumblist *monocle_thumblist)
{
    GList *folder;

    g_mutex_lock(monocle_thumblist->thumb_mutex);

    /* first pretend to delete the visible rows 
     * accomplished by selecting a NULL folder */
    monocle_thumblist_select_folder(monocle_thumblist, NULL);

    /* now just go through and free everything */
    folder = g_list_first(monocle_thumblist->folders);
    if (folder != NULL) {
        do {
            g_list_free_full(((MonocleFolder *)folder->data)->files, g_free);
        } while ((folder = g_list_next(folder)) != NULL);
    }
    g_list_free_full(monocle_thumblist->folders, g_free);
    monocle_thumblist->folders = NULL;

    g_mutex_unlock(monocle_thumblist->thumb_mutex);
}

/* behaves similarly to gtk_list_store_append */
void
monocle_thumblist_append (MonocleThumblist *monocle_thumblist,
                          GtkTreeIter *out, gchar *filename)
{
    GtkTreePath *path;

    MonocleFile *file;
    MonocleFolder *folder;
    gchar *folder_name;

    GList *elem;

    file = g_malloc(sizeof(MonocleFile));
    file->name = g_strdup(filename);
    file->thumbnail = g_object_ref(default_thumbnail);

    folder_name = g_path_get_dirname(filename);
   
    /* search for folder_name in our list */
    folder = NULL;
    elem = g_list_first(monocle_thumblist->folders);
    while (elem != NULL) {
        if (!strcmp(((MonocleFolder *)elem->data)->name, folder_name)) {
            folder = (MonocleFolder *)elem->data;
            break;
        }
        elem = elem->next;
    }

    if (!folder) {
        folder = g_malloc(sizeof(MonocleFolder));
        folder->name = g_strdup(folder_name);
        folder->files = NULL;
        monocle_thumblist->folders = g_list_append(monocle_thumblist->folders, folder);
    }

    folder->files = g_list_append(folder->files, file);

    monocle_thumblist_select_folder(monocle_thumblist, folder);

    /* notify of our insertion */
    path = gtk_tree_path_new();
    gtk_tree_path_append_index(path, g_list_index(folder->files, file));
    out->stamp = monocle_thumblist->stamp;
    out->user_data = file;

    gtk_tree_model_row_inserted (GTK_TREE_MODEL(monocle_thumblist), path, out);
    gtk_tree_path_free(path);
    
    /* queue up to generate thumbnail */
    g_mutex_lock(monocle_thumblist->thumb_mutex);
    g_static_rw_lock_writer_lock(&monocle_thumblist->thumb_rwlock);
    monocle_thumblist_thumbqueue_push(monocle_thumblist, file);
    g_static_rw_lock_writer_unlock(&monocle_thumblist->thumb_rwlock);
    g_mutex_unlock(monocle_thumblist->thumb_mutex);
}

/* convenience (?) functions? */
void
monocle_thumblist_select_folder (MonocleThumblist *monocle_thumblist, MonocleFolder *folder)
{
    GtkTreePath *path;
    gint num_rows, i;

    if (folder == monocle_thumblist->current_folder)
        return;

    /* pretend to delete all the rows */
    if (monocle_thumblist->current_folder != NULL) {
        num_rows = g_list_length(monocle_thumblist->current_folder->files);
        path = gtk_tree_path_new();
        gtk_tree_path_append_index(path, 0);

        for (i = 0; i < num_rows; i++)
            gtk_tree_model_row_deleted (GTK_TREE_MODEL(monocle_thumblist), path);
    }

    monocle_thumblist->current_folder = folder;
    /* TODO: notify of new now-visible rows */
}

void monocle_thumblist_next_folder (MonocleThumblist *monocle_thumblist)
{
    MonocleFolder *folder;
    GList *elem, *next_elem;

    elem = g_list_find(monocle_thumblist->folders, monocle_thumblist->current_folder);
    next_elem = g_list_next(elem);

    if (!next_elem)
        next_elem = g_list_first(monocle_thumblist->folders);
    if (!next_elem)
        return;

    folder = (MonocleFolder *)next_elem->data;
    monocle_thumblist_select_folder(monocle_thumblist, folder);
}

void monocle_thumblist_prev_folder (MonocleThumblist *monocle_thumblist) 
{
    MonocleFolder *folder;
    GList *elem, *prev_elem;

    elem = g_list_find(monocle_thumblist->folders, monocle_thumblist->current_folder);
    prev_elem = g_list_previous(elem);

    if (!prev_elem)
        prev_elem = g_list_last(monocle_thumblist->folders);
    if (!prev_elem)
        return;

    folder = (MonocleFolder *)prev_elem->data;
    monocle_thumblist_select_folder(monocle_thumblist, folder);
}

static void
monocle_file_free (MonocleFile *file) {
    g_free(file->name);
    g_object_unref(file->thumbnail);
    g_free(file);
    file->name = NULL;
    file = NULL;
}

/* Thumbnail Generation */

/* the monocle thumbqueue is basically a bastardized GList
 * items are "pushed" onto the end
 * then "poppped" when a thumbnail is to be generated
 * items are removed along with their counterparts in the thumblist
 *
 * the following thumbqueue functions do NOT accqurie a mutex lock
 * they must be wrapped with g_mutex_lock/unlock() calls */

/* TODO: use more than one thread */
static void
monocle_thumblist_thumbqueue_push (MonocleThumblist *monocle_thumblist, MonocleFile *file)
{
    /* create the thumbnailing thread if needed */
    if (monocle_thumblist->thumb_thread == NULL)
        monocle_thumblist->thumb_thread = g_thread_create ((GThreadFunc)thumbnail_thread_func, monocle_thumblist, FALSE, NULL);

    monocle_thumblist->thumb_queue = g_list_append(monocle_thumblist->thumb_queue, file);

    g_cond_signal(monocle_thumblist->thumb_cond);
}

/* takes a string to remove from the queue */
static void
monocle_thumblist_thumbqueue_remove (MonocleThumblist *monocle_thumblist, gchar *file)
{
    GList *elem;

    elem = g_list_first(monocle_thumblist->thumb_queue);
    while (elem != NULL) {
        if (!strcmp (((MonocleFile *)elem->data)->name, file)) {
            monocle_thumblist->thumb_queue = g_list_delete_link(monocle_thumblist->thumb_queue, elem);
            break;
        }
        elem = g_list_next(elem);
    }
}

/* takes a GSist of strings to remove from the queue */
static void
monocle_thumblist_thumbqueue_remove_many (MonocleThumblist *monocle_thumblist, GSList *files)
{
    GList *elem, *next_elem;

    /* loop through each element in the thumb queue */
    elem = g_list_first(monocle_thumblist->thumb_queue);
    while (elem != NULL) {
        MonocleFile *file = (MonocleFile *)elem->data;
        next_elem = g_list_next(elem); /* needed if elem is removed */

        /* loop through each of our needles */
        GSList *nelem = files;
        while (nelem != NULL) {
            if (!strcmp (file->name, (gchar *)nelem->data)) {
                monocle_thumblist->thumb_queue = g_list_delete_link(monocle_thumblist->thumb_queue, elem);
                break;
            }
            nelem = g_slist_next(nelem);
        }

        elem = next_elem;
    }
}

static void
thumbnail_thread_func (MonocleThumblist *monocle_thumblist) {
    gchar *file;
    GdkPixbuf *thumb;
    GList *elem, *queue_elem;

    GMutex *wait_mutex = g_mutex_new ();
    while (TRUE)
    {

        g_static_rw_lock_reader_lock (&monocle_thumblist->thumb_rwlock);
        /* XXX: this is all really hackish, but it seems like the only way to make the wacky
         * folder system play nice with threading besides needing to do a full search through every loaded file/folder */
        /* "pop" the first element from the queue */
        if (monocle_thumblist->thumb_queue == NULL || g_list_first(monocle_thumblist->thumb_queue) == NULL) {
            g_static_rw_lock_reader_unlock (&monocle_thumblist->thumb_rwlock);

            g_mutex_lock (wait_mutex);
            g_cond_wait (monocle_thumblist->thumb_cond, wait_mutex);
            g_mutex_unlock (wait_mutex);
            continue;
        }
        
        elem = g_list_first(monocle_thumblist->thumb_queue);
        monocle_thumblist->thumb_queue = g_list_remove_link(monocle_thumblist->thumb_queue, elem);
        file = g_strdup(((MonocleFile *)elem->data)->name);

        thumb = generate_thumbnail(file);
        
        if (thumb != NULL) {
            /* if thumb_mutex is locked, the main thread is waiting to modify stuff */
            if (g_mutex_trylock(monocle_thumblist->thumb_mutex)) {
                gint index;
                GtkTreePath *path;
                GtkTreeIter iter;

                /* the following is OKAY since we've locked the thumb_mutex */
                g_static_rw_lock_reader_unlock (&monocle_thumblist->thumb_rwlock);
                g_static_rw_lock_writer_lock (&monocle_thumblist->thumb_rwlock);
                
                ((MonocleFile *)elem->data)->thumbnail = g_object_ref(thumb);
           
                /* notify of our modifications if necessary */
                if (monocle_thumblist->current_folder != NULL) {
                    index = g_list_index(monocle_thumblist->current_folder->files, elem->data);
                    if (index >= 0) {
                        path = gtk_tree_path_new();
                        gtk_tree_path_append_index(path, index);
                        
                        monocle_thumblist_get_iter(GTK_TREE_MODEL(monocle_thumblist), &iter, path);
                        
                        gtk_tree_model_row_changed (GTK_TREE_MODEL(monocle_thumblist), path, &iter);
                        gtk_tree_path_free(path);
                    }
                }

                g_static_rw_lock_writer_unlock (&monocle_thumblist->thumb_rwlock);
                g_mutex_unlock(monocle_thumblist->thumb_mutex);

                g_object_unref(thumb);
            } else {
                /* unfortunately the main thread beat us to the thumbnail lock 
                 * this thumbnail will need to be regenerated (wasteful I know) */
                monocle_thumblist_thumbqueue_push(monocle_thumblist, (MonocleFile *)elem->data);
                g_static_rw_lock_reader_unlock (&monocle_thumblist->thumb_rwlock);
            }
        } else {
            g_static_rw_lock_reader_unlock (&monocle_thumblist->thumb_rwlock);
        }

        g_free(file);

    }
}

/* takes a pointer to a MonocleFile to make a thumbnail for */
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
