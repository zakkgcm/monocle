/* MonocleView implementation file
 * displays image
 * author: cheeseum
 * license: see LICENSE
 */

#include <gdk-pixbuf/gdk-pixbuf.h>
#include "monocleview.h"

#define BUFSIZE 4096
#define g_object_obliterate(x) if (x != NULL && G_IS_OBJECT(x)) { g_object_unref(x); x = NULL; }
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
    gboolean scale_gifs;

    gint     monitor_id;
    gfloat   scale;
    MonocleZoomMode zoom_mode;
} MonocleViewPrivate;

/* The idea is store the original image on load in oimg, never modify it, scale and mangle img as much as you want, use it to redraw to the screen */

G_DEFINE_TYPE(MonocleView, monocle_view, GTK_TYPE_LAYOUT)
static void monocle_view_init (MonocleView *self);
static void monocle_view_class_init (MonocleViewClass *klass);

static gboolean monocle_view_expose     (GtkWidget *widget, GdkEventExpose *event);
static void monocle_view_size_allocate  (GtkWidget *widget, GtkAllocation *allocation);

static gfloat monocle_view_calculate_scale (MonocleView *self);

static void cb_loader_size_prepared     (GdkPixbufLoader *loader, gint width, gint height, MonocleView *self);
static void cb_loader_area_prepared     (GdkPixbufLoader *loader, MonocleView *self);
static void cb_loader_area_updated      (GdkPixbufLoader *loader, gint x, gint y, gint width, gint height, MonocleView *self);
static void cb_loader_closed            (GdkPixbufLoader *loader, MonocleView *self);
static void redraw_image                (MonocleView *self, gint x, gint y, gint width, gint height);

static gboolean cb_advance_anim         (MonocleView *self);
static gboolean write_image_buf         (MonocleView *self);

static void
monocle_view_init (MonocleView *self) {
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    GdkColor black;

    priv->loader     = NULL;
    priv->oimg       = NULL;
    priv->img        = NULL;
    priv->scale      = 0;
    priv->monitor_id = 0;
    priv->isanimated = FALSE;
    priv->scale_gifs = FALSE;
    
    gdk_color_parse("black", &black);
    gtk_widget_modify_bg(GTK_WIDGET(self), GTK_STATE_NORMAL, &black);
}

static void
monocle_view_class_init (MonocleViewClass *klass) {
    GtkWidgetClass *w_class = GTK_WIDGET_CLASS(klass);

    w_class->expose_event    = monocle_view_expose;
    w_class->size_allocate   = monocle_view_size_allocate;
   
    g_type_class_add_private(klass, sizeof(MonocleViewPrivate));
}

MonocleView
*monocle_view_new () {
    MonocleView *view = g_object_new(MONOCLE_TYPE_VIEW, NULL);
    return view;
}

void
monocle_view_set_image (MonocleView *self, gchar *filename) {
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    GtkWidget *widget        = GTK_WIDGET(self);
    
    /* if we were in the middle of loading an image, clean up */
    if (priv->monitor_id != 0) {
        g_source_remove(priv->monitor_id);
        
        gdk_pixbuf_loader_close(priv->loader, NULL);
        g_io_channel_shutdown(priv->io, TRUE, NULL);
        g_io_channel_unref(priv->io);
        priv->io = NULL;
        priv->monitor_id = 0;
    }

    g_object_obliterate(priv->img);
    g_object_obliterate(priv->oimg);
    g_object_obliterate(priv->anim);
    g_object_obliterate(priv->iter);

    priv->isanimated = FALSE;
    gdk_window_clear(GTK_LAYOUT(widget)->bin_window);

    if (filename == NULL)
        return;

    priv->loader = gdk_pixbuf_loader_new();
    g_signal_connect_object(G_OBJECT(priv->loader), "size-prepared", G_CALLBACK(cb_loader_size_prepared), self, 0);
    g_signal_connect_object(G_OBJECT(priv->loader), "area-prepared", G_CALLBACK(cb_loader_area_prepared), self, 0);
    g_signal_connect_object(G_OBJECT(priv->loader), "area-updated",  G_CALLBACK(cb_loader_area_updated), self, 0);
    g_signal_connect_object(G_OBJECT(priv->loader), "closed",        G_CALLBACK(cb_loader_closed), self, 0);

    if ((priv->io = g_io_channel_new_file(filename, "r", NULL)) == NULL)
        return;
   
    g_io_channel_set_encoding(priv->io, NULL, NULL);
    priv->monitor_id = g_idle_add((GSourceFunc)write_image_buf, self);
}

/* This should be a gobject property but LAZY */
void
monocle_view_set_scale (MonocleView *self, gfloat scale) {
    MonocleViewPrivate *priv   = MONOCLE_VIEW_GET_PRIVATE(self);
    
    priv->scale = scale <= 0 ? 1.0 : scale;
    
    if (priv->oimg != NULL)
        monocle_view_scale_image(self);
    return;
}

gfloat
monocle_view_get_scale (MonocleView *self) {
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    return priv->scale;
}

void
monocle_view_set_zoom_mode (MonocleView *self, MonocleZoomMode mode) {
    MonocleViewPrivate *priv   = MONOCLE_VIEW_GET_PRIVATE(self);
    priv->zoom_mode = mode;
    if (priv->oimg) {
        priv->scale = monocle_view_calculate_scale(self);
        monocle_view_scale_image(self);
    }
}

void
monocle_view_set_scale_gifs (MonocleView *self, gboolean scale_gifs) {
    MonocleViewPrivate *priv   = MONOCLE_VIEW_GET_PRIVATE(self);
    priv->scale_gifs = scale_gifs;
    return;
}

gboolean
monocle_view_get_scale_gifs (MonocleView *self) {
    MonocleViewPrivate *priv   = MONOCLE_VIEW_GET_PRIVATE(self);
    return priv->scale_gifs;
}

/* gets desired scale factor based on zoom mode */
gfloat monocle_view_calculate_scale (MonocleView *self) {
    MonocleViewPrivate *priv   = MONOCLE_VIEW_GET_PRIVATE(self);
    GtkWidget          *widget = GTK_WIDGET(self);
    gint pwidth, pheight, wwidth, wheight;
    gfloat scale = priv->scale;

    if (!priv->oimg)
        return 1.0;
    
    pwidth  = gdk_pixbuf_get_width(priv->oimg);
    pheight = gdk_pixbuf_get_height(priv->oimg);
    wwidth  = widget->allocation.width;
    wheight = widget->allocation.height;
    
    if (priv->zoom_mode == MONOCLE_SCALE_FITHEIGHT)
        scale = (pwidth > pheight) ? (gfloat)wwidth/pwidth : (gfloat)wheight/pheight;
    else if (priv->zoom_mode == MONOCLE_SCALE_FITWIDTH)
        scale = (pwidth > pheight) ? (gfloat)wheight/pheight : (gfloat)wwidth/pwidth;
    else if (priv->zoom_mode == MONOCLE_SCALE_1TO1)
        scale = 1.0;
    else if (priv->zoom_mode == MONOCLE_SCALE_CUSTOM)
        scale = priv->scale;
    
    return scale;
}

/* only needs to be called upon change of scale or image, as such, it doesn't check if the image is already scaled */
void
monocle_view_scale_image (MonocleView *self) {
    MonocleViewPrivate *priv   = MONOCLE_VIEW_GET_PRIVATE(self);
    gint swidth, sheight;

    if (!priv->oimg)
        return;

    /*enclosing the entire function in an if looks ugly*/
    if (priv->isanimated && !priv->scale_gifs) {
        redraw_image(self, 0, 0, -1, -1);
        return;
    }
    
    swidth  = (int)(gdk_pixbuf_get_width(priv->oimg) * priv->scale);
    sheight = (int)(gdk_pixbuf_get_height(priv->oimg) * priv->scale);
    
    if (priv->img != NULL)
        g_object_unref(priv->img);
   
    if (priv->scale == 1)
        priv->img = g_object_ref(priv->oimg);
    else
        priv->img = gdk_pixbuf_scale_simple(priv->oimg, swidth, sheight, GDK_INTERP_BILINEAR); /* make asynchronous */

    gtk_layout_set_size(GTK_LAYOUT(self), gdk_pixbuf_get_width(priv->img), gdk_pixbuf_get_height(priv->img));
    redraw_image(self, 0, 0, -1, -1);
}

static gboolean
monocle_view_expose (GtkWidget *widget, GdkEventExpose *event) {
    MonocleView        *self = MONOCLE_VIEW(widget);
    GdkRectangle       area = event->area;
    
    redraw_image(self, area.x, area.y, area.width, area.height);

    return FALSE;
}

static void
monocle_view_size_allocate (GtkWidget *widget, GtkAllocation *allocation) {
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(MONOCLE_VIEW(widget));
   
    GTK_WIDGET_CLASS(monocle_view_parent_class)->size_allocate(widget, allocation);

    if (!priv->oimg)
        return;
    
    /* Causes an infinite loop of scaling if you resize the window too quickly */
    /*if (priv->scale == 0)
        monocle_view_scale_image(MONOCLE_VIEW(widget), 0);*/
}

static void
cb_loader_area_prepared (GdkPixbufLoader *loader, MonocleView *self) {
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    
    /* Handle the image in terms of an animation until we really know what it is */
    priv->anim = gdk_pixbuf_loader_get_animation(loader);
    priv->iter = gdk_pixbuf_animation_get_iter(priv->anim, NULL);
}

static void
cb_loader_size_prepared (GdkPixbufLoader *loader, gint width, gint height, MonocleView *self) {
/*    gtk_layout_set_size(GTK_LAYOUT(self), width, height);*/
    return;
}

static void
cb_loader_area_updated (GdkPixbufLoader *loader, gint x, gint y, gint width, gint height, MonocleView *self) {
     MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
     
     /* have to do this check continuously because of the way pixbufloader works */
     if (!gdk_pixbuf_animation_is_static_image(priv->anim)) {
        priv->isanimated = TRUE;
        if (!gdk_pixbuf_animation_iter_on_currently_loading_frame(priv->iter)) {
            gdk_pixbuf_animation_iter_advance(priv->iter, NULL);              
        }
     }
     
     if (priv->img != NULL)
         g_object_unref(priv->img);
     priv->oimg = gdk_pixbuf_animation_iter_get_pixbuf(priv->iter);
     priv->img = g_object_ref(priv->oimg);
     
     redraw_image(self, x, y, width, height);
}

static void
cb_loader_closed (GdkPixbufLoader *loader, MonocleView *self) {
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    
    g_object_unref(priv->loader);
    priv->loader = NULL;

    if (priv->anim == NULL || priv->oimg == NULL)
        return;

    if (priv->isanimated) {
        g_object_ref(priv->anim);
        g_timeout_add(gdk_pixbuf_animation_iter_get_delay_time(priv->iter), (GSourceFunc)cb_advance_anim, self);
    } else {
        /* we don't need these anymore*/
        /*g_object_unref(priv->iter);*/
        priv->iter = NULL;
        /*g_object_unref(priv->anim);*/
        priv->anim = NULL;
    }
    
    /* might have confused myself here */
    if (!priv->isanimated || (priv->isanimated && priv->scale_gifs)) {
        priv->scale = monocle_view_calculate_scale(self);
        monocle_view_scale_image(self);
    }
    
    redraw_image(self, 0, 0, -1, -1);
}

static gboolean
cb_advance_anim (MonocleView *self) {
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    
    if (!GDK_IS_PIXBUF_ANIMATION_ITER(priv->iter))
        return FALSE;
    
    gdk_pixbuf_animation_iter_advance(priv->iter, NULL);
    
    g_object_unref(priv->img);

    priv->oimg  = gdk_pixbuf_animation_iter_get_pixbuf(priv->iter); /* I know the docs say to copy this but that mem leaks */
    priv->img = g_object_ref(priv->oimg);

    monocle_view_scale_image(self);
    
    g_timeout_add(gdk_pixbuf_animation_iter_get_delay_time(priv->iter), (GSourceFunc)cb_advance_anim, self);

    return FALSE;
}

/* Specify -1 for width/height to use the pixbuf's width/height (ala gdk_draw_pixbuf) */
/* WARNING: HEADACHES AHEAD */
static void
redraw_image (MonocleView *self, gint x, gint y, gint width, gint height) {
    GtkWidget *widget        = GTK_WIDGET(self);
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    GdkRectangle region, pregion;

    if (!priv->img || !priv->oimg)
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

    /* draw that pornography */
    gdk_draw_pixbuf(GTK_LAYOUT(widget)->bin_window, widget->style->black_gc, priv->img, 
                        region.x, region.y, region.x, region.y, region.width, region.height,
                        GDK_RGB_DITHER_NONE,
                        0, 0);
}

static gboolean
write_image_buf (MonocleView *self) {
    MonocleViewPrivate *priv = MONOCLE_VIEW_GET_PRIVATE(self);
    guchar  buf[BUFSIZE];
    gsize   bytes_read;

    g_io_channel_read_chars(priv->io, (gchar *)&buf, BUFSIZE, &bytes_read, NULL);

    if (gdk_pixbuf_loader_write(priv->loader, (const guchar *)&buf, BUFSIZE, NULL) && bytes_read > 0) {
        return TRUE;
    }

    gdk_pixbuf_loader_close(priv->loader, NULL);
    g_io_channel_shutdown(priv->io, TRUE, NULL);
    g_io_channel_unref(priv->io);
    priv->io = NULL;
    priv->monitor_id = 0;
    return FALSE;
}
