/* main header file for monocle
 * cleaner to keep some things here */
#ifndef __MONOCLE_H__
#define __MONOCLE_H__

#include <gtk/gtk.h>
#define LENGTH(X) (sizeof X / sizeof X[0])

static const char *__progname = PROGNAME;
static GtkItemFactoryEntry mainmenu_items[] = {
  { "/_File",               NULL,         NULL,             0, "<Branch>" },
  { "/File/_Open",          "<control>O", NULL,             0, NULL },
  { "/File/_Save",          "<control>S", NULL,             0, NULL },
  { "/File/Save _As",       NULL,         NULL,             0, NULL },
  { "/File/sep1",           NULL,         NULL,             0, "<Separator>" },
  { "/File/Quit",           "<control>Q", gtk_main_quit,    0, NULL },
  { "/_Edit",               NULL,         NULL,             0, "<Branch>" },
  { "/_Options",            NULL,         NULL,             0, "<Branch>" },
  { "/Options/Preferences", NULL,         NULL,             0, NULL },
  { "/_Help",               NULL,         NULL,             0, "<LastBranch>" },
  { "/_Help/About",         NULL,         NULL,             0, NULL },
};

static GtkWidget *create_menubar (GtkWidget *window, GtkItemFactoryEntry *menu_items, gint nmenu_items);
#endif /*__MONOCLE_H__*/
