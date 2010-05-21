#include "monocleview.h"

#define BUFSIZE 4086
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

G_DEFINE_TYPE(MonocleView, monocle_view, GTK_TYPE_DRAWING_AREA)

static void monocle_view_expose         (GtkWidget *widget, GdkEventExpose *event);
static void cb_loader_area_prepared     (GdkPixbufLoader *loader, MonocleView *self);
static void cb_loader_area_updated      (GdkPixbufLoader *loader, gint x, gint y, gint width, gint height, MonocleView *self);
static void redraw_image                (MonocleView *self, gint x, gint y, gint width, gint height);
static gboolean cb_advance_anim         (MonocleView *self);
static gboolean write_image_buf         (MonocleView *self);

static void
monocle_view_init( MonocleView *self ){
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);

    priv->loader   = gdk_pixbuf_loader_new();
    priv->oimg = NULL;
    priv->img  = NULL;
    priv->scale    = 1.0;
    priv->monitor_id = 0;
    g_signal_connect_object(G_OBJECT(priv->loader), "area-prepared", G_CALLBACK(cb_loader_area_prepared), self, 0);
    g_signal_connect_object(G_OBJECT(priv->loader), "area-updated",  G_CALLBACK(cb_loader_area_updated), self, 0);
}

static void
monocle_view_class_init (MonocleViewClass *klass){
    GObjectClass *g_class = G_OBJECT_CLASS(klass); /* I SWEAR I 'LL NEED THIS */
    GtkWidgetClass *w_class = GTK_WIDGET_CLASS(klass);

    w_class->expose_event = monocle_view_expose; /* >monocleview.c:53: warning: assignment from incompatible pointer type :| */

    g_type_class_add_private(klass, sizeof(MonocleViewPrivate));
}

void
monocle_view_set_image(MonocleView *self, gchar *filename){
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    
    if((priv->io = g_io_channel_new_file(filename, "r", NULL)) == NULL)
        return;
   
    g_io_channel_set_encoding(priv->io, NULL, NULL);
    priv->monitor_id = g_idle_add((GSourceFunc)write_image_buf, self);

}

void
monocle_view_scale_image( MonocleView *self, gfloat scale ){
    MonocleViewPrivate *priv   = MONOCLE_VIEW_GET_PRIVATE(self);
    GtkWidget          *widget = GTK_WIDGET(self);
    gint width  = gdk_pixbuf_get_width(priv->oimg);
    gint height = gdk_pixbuf_get_height(priv->oimg);
   

    if(scale == priv->scale)
        return;

    /* A scale of 0 means fit to window */
    if(scale > 0.0)
        priv->scale = scale;
    else
        priv->scale = (width > height) ? (double)widget->allocation.width/width : (double)widget->allocation.height/height;

    g_object_unref(priv->img);
    priv->img = gdk_pixbuf_scale_simple(priv->oimg, (int)(gdk_pixbuf_get_width(priv->oimg) * priv->scale), 
                                                    (int)(gdk_pixbuf_get_height(priv->oimg) * priv->scale), 
                                                    GDK_INTERP_BILINEAR);

    redraw_image(self, 0, 0, -1, -1);
}

static void
monocle_view_expose( GtkWidget *widget, GdkEventExpose *event ){
    MonocleView        *self = MONOCLE_VIEW(widget);
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    GdkRectangle       area = event->area;

    if(priv->img)
        redraw_image(self, area.x, area.y, area.width, area.height);
}

static void
cb_loader_area_prepared( GdkPixbufLoader *loader, MonocleView *self ){
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    GdkPixbufAnimation *a;

    a = gdk_pixbuf_loader_get_animation(loader);
    if(gdk_pixbuf_animation_is_static_image(a)){
        /* THIS IS NO ANIMATION */
        priv->oimg = gdk_pixbuf_loader_get_pixbuf(loader);
        priv->isanimated = FALSE;
    }else{
        priv->iter = gdk_pixbuf_animation_get_iter(a, NULL);
        priv->anim = a;
        priv->isanimated = TRUE;
        g_timeout_add(gdk_pixbuf_animation_iter_get_delay_time(priv->iter), (GSourceFunc)cb_advance_anim, self);
    }
}

static void
cb_loader_area_updated( GdkPixbufLoader *loader, gint x, gint y, gint width, gint height, MonocleView *self ){
     MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    
     /*printf("update: %d, %d, %d, %d\n", x, y, width, height);*/
     /* Animations only need to redraw if we're currently viewing the frame being loaded */
     if(priv->isanimated && !gdk_pixbuf_animation_iter_on_currently_loading_frame(priv->iter))
         return;
     if(priv->img)
         g_object_unref(priv->img);

     g_object_ref(priv->oimg);
     priv->img =  g_object_ref(priv->oimg);

     redraw_image(self, x, y, width, height);
}

static gboolean
cb_advance_anim( MonocleView *self ){
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
   
    g_object_unref(priv->img);
    gdk_pixbuf_animation_iter_advance(priv->iter, NULL);
    priv->oimg  = gdk_pixbuf_copy(gdk_pixbuf_animation_iter_get_pixbuf(priv->iter));
    priv->img   = gdk_pixbuf_scale_simple(priv->oimg, (int)(gdk_pixbuf_get_width(priv->oimg) * priv->scale), 
                                                     (int)(gdk_pixbuf_get_height(priv->oimg) * priv->scale), 
                                                     GDK_INTERP_BILINEAR);
    redraw_image(self, 0, 0, -1, -1);
    
    g_timeout_add(gdk_pixbuf_animation_iter_get_delay_time(priv->iter), (GSourceFunc)cb_advance_anim, self);

    return FALSE;
}

static void
redraw_image( MonocleView *self, gint x, gint y, gint width, gint height ){
    GtkWidget *widget        = GTK_WIDGET(self);
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    GdkRectangle region, pregion;

    /* We're not redrawing the whole image */
    if(width >= 0 && height >= 0){
        region.x      = x;
        region.y      = y;
        region.width  = width;
        region.height = height;

        pregion.x = 0;
        pregion.y = 0;
        pregion.width = gdk_pixbuf_get_width(priv->img);
        pregion.height = gdk_pixbuf_get_height(priv->img);
        gdk_rectangle_intersect(&region, &pregion, &region);
    }

    /*if(width > gdk_pixbuf_get_width(priv->img))
        width = -1;
    if(height > gdk_pixbuf_get_height(priv->img))
        height = -1;*/

    printf("%d, %d, %d, %d, %d, %d\n", x, y, width, height, gdk_pixbuf_get_width(priv->img), gdk_pixbuf_get_height(priv->img));

    gdk_draw_pixbuf(widget->window, widget->style->white_gc, priv->img, 
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
    priv->monitor_id = 0;
    return FALSE;
}
