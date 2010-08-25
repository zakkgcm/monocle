#include "monocleview.h"

#define BUFSIZE 4096
#define MONOCLE_VIEW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), MONOCLE_TYPE_VIEW, MonocleViewPrivate))

enum {
    PROP_0,
    PROP_MONOCLEVIEW_SCALE
};

typedef struct _MonocleViewPrivate {
    GIOChannel         *io; /* for getting image data in a non-blocking manner */
    GdkPixbufLoader    *loader; /* for loading image */
    GdkPixbuf          *oimg;  /* copy of the original loaded image or current frame in the case of an animation, keeps scaling from being destructive */
    GdkPixbuf          *img;   /* The image being displayed currently */
    GdkPixbufAnimation *anim;
    GdkPixbufAnimationIter *iter;

    gboolean isanimated;
    gint     monitor_id;
    gfloat   scale;
} MonocleViewPrivate;

/* The idea is store the original image on load in oimg, never modify it, scale and mangle img as much as you want, use it to redraw to the screen */

G_DEFINE_TYPE(MonocleView, monocle_view, GTK_TYPE_LAYOUT)

static gboolean monocle_view_expose     (GtkWidget *widget, GdkEventExpose *event);
static gboolean monocle_view_configure  (GtkWidget *widget, GdkEventConfigure *event);
static void cb_loader_area_prepared     (GdkPixbufLoader *loader, MonocleView *self);
static void cb_loader_area_updated      (GdkPixbufLoader *loader, gint x, gint y, gint width, gint height, MonocleView *self);
static void cb_loader_closed            (GdkPixbufLoader *loader, MonocleView *self);
static void redraw_image                (MonocleView *self, gint x, gint y, gint width, gint height);
static gboolean cb_advance_anim         (MonocleView *self);
static gboolean write_image_buf         (MonocleView *self);

static void
monocle_view_init( MonocleView *self ){
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    
    priv->loader     = NULL;
    priv->oimg       = NULL;
    priv->img        = NULL;
    priv->scale      = 0;
    priv->monitor_id = 0;
    priv->isanimated = FALSE;

    /* g_signal_connect(G_OBJECT(GTK_LAYOUT(self)), "configure-event", G_CALLBACK(monocle_view_configure), NULL);  doesn't work */
}

static void
monocle_view_class_init (MonocleViewClass *klass){
    GObjectClass *g_class = G_OBJECT_CLASS(klass); /* I SWEAR I 'LL NEED THIS */
    GtkWidgetClass *w_class = GTK_WIDGET_CLASS(klass);

    w_class->expose_event    = monocle_view_expose;
    /*class->configure_event = monocle_view_configure; gtklayout cannot configure event*/ 

    g_type_class_add_private(klass, sizeof(MonocleViewPrivate));
}

void
monocle_view_set_image(MonocleView *self, gchar *filename){
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    GtkWidget *widget        = GTK_WIDGET(self);
    
    /* Kind of gross looking but a wrapper seems unnecessary */
    if(priv->img != NULL){
        g_object_unref(priv->img);
        priv->img = NULL;
    }
    if(priv->oimg != NULL){
        g_object_unref(priv->oimg);
        priv->oimg = NULL;
    }
    if(priv->anim != NULL){
        g_object_unref(priv->anim);
        priv->anim = NULL;
    }
    if(priv->iter != NULL){
        g_object_unref(priv->iter);
        priv->iter = NULL;
    }
    
    priv->isanimated = FALSE;

    priv->loader = gdk_pixbuf_loader_new();
    g_signal_connect_object(G_OBJECT(priv->loader), "area-prepared", G_CALLBACK(cb_loader_area_prepared), self, 0);
    g_signal_connect_object(G_OBJECT(priv->loader), "area-updated",  G_CALLBACK(cb_loader_area_updated), self, 0);
    g_signal_connect_object(G_OBJECT(priv->loader), "closed",        G_CALLBACK(cb_loader_closed), self, 0);

    if((priv->io = g_io_channel_new_file(filename, "r", NULL)) == NULL)
        return;
   
    g_io_channel_set_encoding(priv->io, NULL, NULL);
    priv->monitor_id = g_idle_add((GSourceFunc)write_image_buf, self);
    
    gdk_window_clear(GTK_LAYOUT(widget)->bin_window);
}

/* This should be a gobject property but LAZY */
void
monocle_view_set_scale( MonocleView *self, gfloat scale ){
    MonocleViewPrivate *priv   = MONOCLE_VIEW_GET_PRIVATE(self);
    priv->scale = scale;
    return;
}
void
monocle_view_scale_image( MonocleView *self, gfloat scale ){
    MonocleViewPrivate *priv   = MONOCLE_VIEW_GET_PRIVATE(self);
    GtkWidget          *widget = GTK_WIDGET(self);

    gint pwidth  = gdk_pixbuf_get_width(priv->oimg);
    gint pheight = gdk_pixbuf_get_height(priv->oimg);
    gint swidth, sheight;

    /* A scale of < 0 means fit to window */
    /* change this to some kind of constant like MONOCLE_VIEW_SCALE_FIT or something */
    if(scale > 0){
        priv->scale = scale;
    }else{
        priv->scale = 0;
        scale = (pwidth > pheight) ? (double)widget->allocation.width/pwidth : (double)widget->allocation.height/pheight;
    }

    swidth  = (int)(pwidth * scale);
    sheight = (int)(pheight * scale);
    if((swidth == pwidth || sheight == pheight) && scale != 1) /* WOOT ARE YA DOOIN YEH NINNY */
        return;

    if(priv->img != NULL)
        g_object_unref(priv->img);

    priv->img = gdk_pixbuf_scale_simple(priv->oimg, swidth, sheight, GDK_INTERP_BILINEAR); /* make asynchronous */
    
    gtk_layout_set_size(GTK_LAYOUT(self), gdk_pixbuf_get_width(priv->img), gdk_pixbuf_get_height(priv->img));
    redraw_image(self, 0, 0, -1, -1);
}

static gboolean
monocle_view_expose( GtkWidget *widget, GdkEventExpose *event ){
    MonocleView        *self = MONOCLE_VIEW(widget);
    GdkRectangle       area = event->area;
    
    redraw_image(self, area.x, area.y, area.width, area.height);

    return FALSE;
}

static gboolean
monocle_view_configure( GtkWidget *widget, GdkEventConfigure *event ){
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(MONOCLE_VIEW(widget));
    /* If we're set to fit-to-window then rescale the image on window resize */
    /* Inadvertantly makes ugly flashing lines when we clear the window and redraw so often, fix it */
    if(!priv->oimg)
        return FALSE;

    if(priv->scale == 0)
        monocle_view_scale_image(MONOCLE_VIEW(widget), 0);

    return FALSE;
}

static void
cb_loader_area_prepared( GdkPixbufLoader *loader, MonocleView *self ){
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    
    /* Handle the image in terms of an animation until we really know what it is */
    priv->anim = gdk_pixbuf_loader_get_animation(loader);
    g_object_ref(priv->anim);
    priv->iter = gdk_pixbuf_animation_get_iter(priv->anim, NULL);
    g_object_ref(priv->iter);
}

static void
cb_loader_area_updated( GdkPixbufLoader *loader, gint x, gint y, gint width, gint height, MonocleView *self ){
     MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
     
     /* have to do this check continuously because of the way pixbufloader works */
     if(!gdk_pixbuf_animation_is_static_image(priv->anim)){
        priv->isanimated = TRUE;
        if(!gdk_pixbuf_animation_iter_on_currently_loading_frame(priv->iter)){
            gdk_pixbuf_animation_iter_advance(priv->iter, NULL);              
        }
     }
     
     if(priv->img != NULL)
         g_object_unref(priv->img);

     priv->oimg = gdk_pixbuf_animation_iter_get_pixbuf(priv->iter);
     priv->img = g_object_ref(priv->oimg);

     redraw_image(self, x, y, width, height);
}

static void
cb_loader_closed( GdkPixbufLoader *loader, MonocleView *self ){
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    
    if(priv->isanimated){
        g_timeout_add(gdk_pixbuf_animation_iter_get_delay_time(priv->iter), (GSourceFunc)cb_advance_anim, self);
    }else{
        /* we don't need these anymore*/
        g_object_unref(priv->iter);
        priv->iter = NULL;
        g_object_unref(priv->anim);
        priv->anim = NULL;
    }

    g_object_unref(priv->loader);
    priv->loader = NULL;

    monocle_view_scale_image(self, priv->scale);
    redraw_image(self, 0, 0, -1, -1);
}

static gboolean
cb_advance_anim( MonocleView *self ){
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    
    if(!GDK_IS_PIXBUF_ANIMATION_ITER(priv->iter))
        return FALSE;
    
    gdk_pixbuf_animation_iter_advance(priv->iter, NULL);
    
    if(priv->img != NULL)
        g_object_unref(priv->img);

    priv->oimg  = gdk_pixbuf_animation_iter_get_pixbuf(priv->iter); /* I know the docs say to copy this but that mem leaks */
    priv->img = g_object_ref(priv->oimg);

    monocle_view_scale_image(self, priv->scale);
    
    g_timeout_add(gdk_pixbuf_animation_iter_get_delay_time(priv->iter), (GSourceFunc)cb_advance_anim, self);

    return FALSE;
}

/* Specify -1 for width/height to use the pixbuf's width/height (ala gdk_draw_pixbuf) */
static void
redraw_image( MonocleView *self, gint x, gint y, gint width, gint height ){
    GtkWidget *widget        = GTK_WIDGET(self);
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    GdkRectangle region, pregion;
    
    if(!priv->img || !priv->oimg)
        return;

    pregion.x = 0;
    pregion.y = 0;
    pregion.width = gdk_pixbuf_get_width(priv->img);
    pregion.height = gdk_pixbuf_get_height(priv->img);

    region.x      = x;
    region.y      = y;
    region.width  = (width == -1) ? pregion.width: width;    
    region.height = (height == -1) ? pregion.height: height;
    
    /* Only draw parts of the pixbuf that actually exist */
    gdk_rectangle_intersect(&region, &pregion, &region);

    /* ignore all this */
    /* change the size of the widget to the size of the pixbuf where it is inside the window if we're asked to redraw it completely
     * gets rid of leftovers when scaling down from a larger size 
     * Problem is screws up the way the window can be resized 
     * Scrolled window may fix that
     */
    
    /*if(width == -1 || height == -1){
        GdkRectangle cregion;
        cregion.x = 0;
        cregion.y = 0;
        cregion.width  = widget->allocation.width;
        cregion.height = widget->allocation.height;
        gdk_rectangle_intersect(&cregion, &pregion, &cregion);
        
        gtk_widget_set_size_request(widget, cregion.width, cregion.height);
    }*/
    /* end ignore */

    /* why this causes spastic flashing I don't know, possibly the pixbuf is taking just a tad too long to draw */
    /* might be fixable with a temp composition pixmap, but could be too much trouble */
    gdk_draw_rectangle(GTK_LAYOUT(widget)->bin_window, widget->style->black_gc, TRUE, 0, 0, widget->allocation.width, widget->allocation.height); 
    gdk_draw_pixbuf(GTK_LAYOUT(widget)->bin_window, widget->style->black_gc, priv->img, 
                        region.x, region.y, region.x, region.y, region.width, region.height,
                        GDK_RGB_DITHER_NONE,
                        0, 0);
}

static gboolean
write_image_buf( MonocleView *self ){
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    guchar  buf[BUFSIZE];
    gsize   bytes_read;

    g_io_channel_read_chars(priv->io, (gchar *)&buf, BUFSIZE, &bytes_read, NULL);

    if(gdk_pixbuf_loader_write(priv->loader, (const guchar *)&buf, BUFSIZE, NULL) && bytes_read > 0){
        return TRUE;
    }
   
    gdk_pixbuf_loader_close(priv->loader, NULL);
    g_io_channel_shutdown(priv->io, TRUE, NULL);
    g_io_channel_unref(priv->io);
    priv->io = NULL;
    priv->monitor_id = 0;
    return FALSE;
}
