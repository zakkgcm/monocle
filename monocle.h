/* main header file for monocle
 * cleaner to keep some things here */
#ifndef __MONOCLE_H__
#define __MONOCLE_H__

#include <gtk/gtk.h>
#include "image.h"
#define LENGTH(X) (sizeof X / sizeof X[0])

static const char *__progname = PROGNAME;

static void scale_image_cb (gpointer callback_data, guint callback_action, GtkWidget *menu_item);
static GtkItemFactoryEntry mainmenu_items[] = {
  { "/_File",               NULL,         NULL,                 0, "<Branch>" },
  { "/File/_Open",          "<control>O", NULL,                 0, NULL },
  { "/File/_Save",          "<control>S", NULL,                 0, NULL },
  { "/File/Save _As",       NULL,         NULL,                 0, NULL },
  { "/File/sep1",           NULL,         NULL,                 0, "<Separator>" },
  { "/File/Quit",           "<control>Q", gtk_main_quit,        0, NULL },
  { "/_View",               NULL,         NULL,                 0, "<Branch>" },
  { "/View/Fit to Window",  NULL,         scale_image_cb,       0, NULL},
  { "/View/Zoom 1x",        NULL,         scale_image_cb,       1, NULL},
  { "/View/Zoom In 2x",     NULL,         scale_image_cb,       4, NULL},
  { "/View/Zoom In 4x",     NULL,         scale_image_cb,       5, NULL},
  { "/View/Zoom Out 2x",    NULL,         scale_image_cb,       2, NULL},
  { "/View/Zoom Out 4x",    NULL,         scale_image_cb,       3, NULL},
  { "/_Options",            NULL,         NULL,                 0, "<Branch>" },
  { "/Options/Preferences", NULL,         NULL,                 0, NULL },
  { "/_Help",               NULL,         NULL,                 0, "<LastBranch>" },
  { "/_Help/About",         NULL,         NULL,                 0, NULL },
};

static GtkWidget *create_menubar (GtkWidget *window, GtkItemFactoryEntry *menu_items, gint nmenu_items);
#endif /*__MONOCLE_H__*/
