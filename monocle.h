/* main header file for monocle
 * cleaner to keep some things here */
#ifndef __MONOCLE_H__
#define __MONOCLE_H__

#include <gtk/gtk.h>
#define LENGTH(X) (sizeof X / sizeof X[0])

static const char *__progname = PROGNAME;

static gboolean monocle_quit ();

static void action_open_file ();
static void action_open_folder ();
static void action_edit_preferences ();

static void action_scale_menu (GtkRadioAction *action, GtkRadioAction *current, gpointer user_data);
static void action_zoom_in ();
static void action_zoom_out ();
static void action_sort_menu (GtkRadioAction *action, GtkRadioAction *current, gpointer user_data);

static gboolean cb_thumbpane_addrmbutton (GtkWidget *button, GdkEventButton *event, gpointer user_data);

static const gchar *monocle_ui =
"<ui>"
"    <menubar name='MainMenubar'>"
"        <menu name='FileMenu' action='FileMenuAction'>"
"            <menuitem name='Open'        action='OpenFile'/>"
"            <menuitem name='Open Folder' action='OpenFolder'/>"
"            <menuitem name='Quit'        action='QuitAction'/>"
"        </menu>"
"        <menu name='EditMenu' action='EditMenuAction'>"
"            <menuitem name='Preferences' action='EditPreferences'/>"
"        </menu>"
"        <menu name='ViewMenu' action='ViewMenuAction'>"
"            <menuitem name='Fit to Width'   action='Scale_FitWidth'/>"
"            <menuitem name='Fit to Height'  action='Scale_FitHeight'/>"
"            <menuitem name='Cusom Zoom'     action='Scale_Custom'/>"
"            <menuitem name='Zoom In'        action='Zoom_In'/>"
"            <menuitem name='Zoom Out'       action='Zoom_Out'/>"
"            <menuitem name='Zoom 1x'        action='Scale_1x'/>"
"        </menu>"
"        <menu name='SortMenu' action='SortMenuAction'>"
"            <menuitem name='Sort By Name'              action='SortName'/>"
"            <menuitem name='Sort By Modification Date' action='SortDate'/>"
"            <menuitem name='Sort By Size'              action='SortSize'/>"
"            <separator/>"
"            <menuitem name='Ascending'  action='SortAscending'/>"
"            <menuitem name='Descending' action='SortDescending'/>"
"        </menu>"
"    </menubar>"
"        <accelerator name='kp_add' action='Zoom_In_KPadd'/>"
"        <accelerator name='equal' action='Zoom_In_Equal'/>"
"        <accelerator name='kp_subtract' action='Zoom_Out_KPsubtract'/>"
"</ui>";

static GtkActionEntry main_entries[] = {
    { "FileMenuAction", NULL, "_File" },
    { "EditMenuAction", NULL, "_Edit" },
    { "ViewMenuAction", NULL, "_View" },
    { "SortMenuAction", NULL, "_Sort" },

    { "OpenFile", GTK_STOCK_OPEN,
      "_Open", "<Control>O",
      "Open a File",
      G_CALLBACK(action_open_file) },

    { "OpenFolder", GTK_STOCK_OPEN,
      "Open Folder", "<Control><Alt>O",
      "Open a Folder",
      G_CALLBACK(action_open_folder) },
    
    { "EditPreferences", GTK_STOCK_PREFERENCES,
      "Preferences", NULL,
      "Edit Preferences",
      G_CALLBACK(action_edit_preferences) },

    { "QuitAction", GTK_STOCK_QUIT,
      "Quit", "<Control>Q",
      "Exit Monocle",
      G_CALLBACK(monocle_quit) },
};

/* this is fucking confusing, use scale OR zoom not both */
static GtkRadioActionEntry zoom_entries[] = {
    { "Scale_1x", NULL,
      "Zoom 1x", NULL,
      "1x zoom", 1 },
    
    { "Scale_FitHeight", NULL,
      "Fit to Height", NULL,
      "Fit Image to Height of Window", 2 },
    
    { "Scale_FitWidth", NULL,
      "Fit to Width", NULL,
      "Fit Image to Width of Window", 3 },

    { "Scale_Custom", NULL,
      "Custom Zoom", NULL,
      "Set a custom zoom level", 4 },

};

/* urhgurhgurhg all these accelerators */
static GtkActionEntry zoom_inout_entries[] = {
    { "Zoom_In", NULL,
      "Zoom In", "plus",
      "Zoom In", G_CALLBACK(action_zoom_in) },
      
    { "Zoom_In_KPadd", NULL,
      "Zoom In", "KP_Add",
      "Zoom In", G_CALLBACK(action_zoom_in) },
    
    { "Zoom_In_Equal", NULL,
      "Zoom In", "equal",
      "Zoom In", G_CALLBACK(action_zoom_in) },
    
    { "Zoom_Out", NULL,
      "Zoom Out", "minus",
      "Zoom Out", G_CALLBACK(action_zoom_out) },
 
    { "Zoom_Out_KPsubtract", NULL,
      "Zoom Out", "KP_Subtract",
      "Zoom Out", G_CALLBACK(action_zoom_out) },
};

static GtkRadioActionEntry sorttype_entries[] = {
    { "SortName", NULL,
      "Sort By Name", NULL,
      "Sort Images by Filename", 1 },
    
    { "SortDate", NULL,
      "Sort By Modification Date", NULL,
      "Sort Images by Modification Date", 2 },

    { "SortSize", NULL,
      "Sort By Filesize", NULL,
      "Sort Images by Filesize", 3 },
};

static GtkRadioActionEntry sortdirection_entries[] = {
    { "SortAscending", NULL,
      "Ascending", NULL,
      "Sort Images in Ascending Order", -1 },

    { "SortDescending", NULL,
      "Descending", NULL,
      "Sort Images in Descending Order", -2 },
};

#endif /*__MONOCLE_H__*/
