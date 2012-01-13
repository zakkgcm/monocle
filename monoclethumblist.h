#ifndef __MONOCLE_THUMBLIST_H__
#define __MONOCLE_THUMBLIST_H__

# include <gtk/gtk.h>

/* internal Thumblist data structures */

typedef struct _MonocleFile MonocleFile;
typedef struct _MonocleFolder MonocleFolder;

struct _MonocleFile
{
    gchar *name;
    GdkPixbuf *thumbnail;
};

struct _MonocleFolder
{
    gchar *name;
    GList *sub_folders; /* list of MonocleFolder */
    GList *files;       /* list of MonocleFile */
};

enum 
{
    MONOCLE_THUMBLIST_COL_FILENAME = 0,
    MONOCLE_THUMBLIST_COL_THUMBNAIL,
    MONOCLE_THUMBLIST_N_COLS
};

/* GObject data structures */

#define MONOCLE_TYPE_THUMBLIST                  (monocle_thumblist_get_type ())
#define MONOCLE_THUMBLIST(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONOCLE_TYPE_THUMBLIST, MonocleThumblist))
#define MONOCLE_IS_THUMBLIST(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MONOCLE_TYPE_THUMBLIST))
#define MONOCLE_THUMBLIST_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), MONOCLE_TYPE_THUMBLIST, MonocleThumblistClass))
#define MONOCLE_IS_THUMBLIST_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), MONOCLE_TYPE_THUMBLIST))
#define MONOCLE_THUMBLIST_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), MONOCLE_TYPE_THUMBLIST, MonocleThumblistClass)

typedef struct _MonocleThumblist      MonocleThumblist;
typedef struct _MonocleThumblistClass MonocleThumblistClass;

struct _MonocleThumblist
{
    GObject parent;
   
    /* FIXME: put these in a priv */
    GMutex          *thumb_mutex; /* XXX: used to prevent race conditions while preserving speed */
    GStaticRWLock   thumb_rwlock;
    GCond           *thumb_cond;
    //GAsyncQueue *thumb_queue;
    GList           *thumb_queue;
    GThread         *thumb_thread;
    //GThreadPool *thread_pool;

    MonocleFolder *root_folder;     /* root of our internal file heirarchy */
    GList         *folders;         /* all of the folders, just for figuring out interface */
    /* FIXME: a current_folder_pos would hasten many things */
    MonocleFolder *current_folder;  /* currently viewed folder */

    guint num_rows;
    gint stamp;
};

struct _MonocleThumblistClass
{
    GObjectClass parent_class;

    GdkPixbuf *default_thumbnail;
};

/* function decls */

GType monocle_thumblist_get_type (void);
MonocleThumblist *monocle_thumblist_new ();

void monocle_thumblist_append (MonocleThumblist *monocle_thumblist, GtkTreeIter *out, gchar *filename);

gboolean monocle_thumblist_remove                (MonocleThumblist *monocle_thumblist, GtkTreeIter *iter);
gboolean monocle_thumblist_remove_current_folder (MonocleThumblist *monocle_thumblist, MonocleFolder **out);
void     monocle_thumblist_clear                 (MonocleThumblist *monocle_thumblist);

void monocle_thumblist_select_folder (MonocleThumblist *monocle_thumblist, MonocleFolder *folder);
void monocle_thumblist_next_folder (MonocleThumblist *monocle_thumblist);
void monocle_thumblist_prev_folder (MonocleThumblist *monocle_thumblist);
#endif /* __MONOCLE_THUMBLIST_H__ */
