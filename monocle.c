/* main monocle implementation 
 * author: cheeseum
 * license: see LICENSE
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <gtk/gtk.h>

#include "monocle.h"
#include "monocleview.h"
#include "monoclethumbpane.h"

MonocleView *image;
MonocleThumbpane *thumbpane;
GtkWidget *window;

static GtkWidget 
*create_menubar( GtkWidget *window, GtkItemFactoryEntry *menu_items, gint nmenu_items ){
    GtkAccelGroup *accel_group = gtk_accel_group_new();
    GtkItemFactory *item_factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>", accel_group);

    gtk_item_factory_create_items(item_factory, nmenu_items, menu_items, NULL);
    gtk_window_add_accel_group(GTK_WINDOW (window), accel_group);
    
    return gtk_item_factory_get_widget(item_factory, "<main>");
}

static void
cb_set_image (GtkWidget *widget, gchar *filename, gpointer data){
    /* what am I even DOING this is absurd */
    gchar *newtitle;
    newtitle = g_malloc(strlen(filename) + 11);
    sprintf(newtitle, "%s - Monocle", filename);
    gtk_window_set_title(GTK_WINDOW(window), (const gchar *) newtitle); 
    g_free(newtitle);

    monocle_view_set_image(image, filename);
}

/* File Stuff */
static void
cb_open_file (gpointer callback_data, guint callback_action, GtkWidget *menu_item){
    GtkWidget *chooser = gtk_file_chooser_dialog_new("Open Image(s)", GTK_WINDOW(window), 
                                GTK_FILE_CHOOSER_ACTION_OPEN,
                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				                GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				                NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), TRUE);

    if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT){ 
        GSList *files;
        files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER (chooser)); /*get uris doesn't encode right for thumbnails*/
        monocle_thumbpane_add_many(thumbpane, files);
        g_slist_free (files);
    }
    gtk_widget_destroy (chooser);
    
    return;
}

static void
cb_open_folder (gpointer callback_data, guint callback_action, GtkWidget *menu_item){
    GtkWidget *recursive_toggle = gtk_check_button_new_with_label("Open Recursively?");
    GtkWidget *chooser = gtk_file_chooser_dialog_new("Open Folder", GTK_WINDOW(window), 
                                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				                GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				                NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), TRUE);
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(chooser), recursive_toggle);
    
    if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT){ 
        gchar *folder;
        gboolean recursive_load = FALSE;
        if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(recursive_toggle)))
            recursive_load = TRUE;
        folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (chooser));
        monocle_thumbpane_add_folder(thumbpane, folder, recursive_load);
        g_free (folder);
    }
    gtk_widget_destroy (chooser);
    
    return;
}

/* Menu callbacks */
static void cb_scale_image (gpointer callback_data, guint callback_action, GtkWidget *menu_item){
    gfloat scale;
    switch(callback_action) {
        case 0:
            scale = 0;
            break;
        case 1:
            scale = 1.0;
            break;
        case 2:
            scale = 0.5;
            break;
        case 3:
            scale = 0.25;
            break;
        case 4:
            scale = 2;
            break;
        case 5:
            scale = 4;
            break;
        default:
            scale = 1.0;
    }
    monocle_view_scale_image(image, scale);
    return;
}

static void
cb_set_sorting (gpointer callback_data, guint callback_action, GtkWidget *menu_item){
    switch(callback_action) {
        case 0:
            monocle_thumbpane_sort_by_name (thumbpane);
            break;
        case 1:
            monocle_thumbpane_sort_by_date (thumbpane);
            break;
        case 2:
            monocle_thumbpane_sort_by_size (thumbpane);
            break;
        case 3:
            monocle_thumbpane_sort_order_ascending (thumbpane);
            break;
        case 4:
            monocle_thumbpane_sort_order_descending (thumbpane);
            break;
    }
    return;
}

/* Button Callbacks */
static gboolean
cb_thumbpane_addrmbutton (GtkWidget *button, GdkEventButton *event, gpointer user_data){
    if(user_data == 0){
        monocle_thumbpane_remove_current(thumbpane);
    }
    return FALSE;
}

static void usage (){
    fprintf(stderr,
            "usage: %s [args] [imagefile/folder]\n"
            "\t-R               Recursively load files from a directory\n"
            "\t-s [scale]       Set the initial scale, 'fit' or 0 for fit to window (default)\n"
            "\t-v               Show version information\n"
            "\t-h               Show this help message\n",
            __progname
           );
    exit(EXIT_FAILURE);
}


int main (int argc, char *argv[]){
    GtkWidget *vbox, *hbox, *vthumbbox, *hthumbbox,
              *menubar, *view_win,
              *thumbadd, *thumbrm;
    gchar filearg[PATH_MAX+1];
    float scale = 1;
    gboolean recursive_load = FALSE;

    int optc;
    extern char *optarg;
    while((optc = getopt(argc, argv, "hvRs:")) != EOF)
        switch(optc) {
            case 'R':
                recursive_load = TRUE;
                break;
            case 's':
                if(!strcmp(optarg, "fit")){
                    scale = 0;
                    printf("Setting scale to fit to window\n");
                }else if(atof(optarg) > 0){ 
                    scale = atof(optarg);
                    printf("Setting scale to %.1f\n", scale);
                }else{
                    printf("Unknown scale %s, defaulting to fit to window\n", optarg);
                }
                break;
            case 'v':
                printf("Monocle Version: %s\n", VERSION);
            case 'h':
            default:
                usage();
                exit(EXIT_FAILURE);
        }

    /* Gtk Setup */
    gtk_init(&argc, &argv);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(G_OBJECT(window), "delete_event", G_CALLBACK(gtk_main_quit), NULL);
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 500);

    /* Thumbpane VBox and Buttons */
    thumbpane = g_object_new(MONOCLE_TYPE_THUMBPANE, NULL);
    g_signal_connect(G_OBJECT(thumbpane), "image-changed", G_CALLBACK(cb_set_image), NULL);

    vthumbbox = gtk_vbox_new(FALSE, 1);
    hthumbbox = gtk_hbutton_box_new();
    thumbadd    = gtk_button_new_from_stock(GTK_STOCK_ADD);
    thumbrm     = gtk_button_new_from_stock(GTK_STOCK_REMOVE);

    g_signal_connect(G_OBJECT(thumbadd), "button-release-event", G_CALLBACK(cb_thumbpane_addrmbutton), GINT_TO_POINTER(1));
    g_signal_connect(G_OBJECT(thumbrm), "button-release-event", G_CALLBACK(cb_thumbpane_addrmbutton), GINT_TO_POINTER(0));
 

    gtk_box_pack_start(GTK_BOX(hthumbbox), thumbadd, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hthumbbox), thumbrm, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vthumbbox), GTK_WIDGET(thumbpane), TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vthumbbox), hthumbbox, FALSE, FALSE, 0);

    /* Main VBox */
    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_border_width(GTK_CONTAINER (vbox), 1);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    
    /* HBox, contains monocleview and thumbpane vbox*/
    hbox = gtk_hbox_new(FALSE, 1);
    gtk_container_border_width(GTK_CONTAINER (hbox), 1);

    menubar = create_menubar(window, mainmenu_items, LENGTH(mainmenu_items));
    image = g_object_new(MONOCLE_TYPE_VIEW, NULL);
    monocle_view_set_scale(image, scale);
    
    gtk_widget_set_size_request(GTK_WIDGET(thumbpane), 150, -1);

    /* Contain the MonocleView */
    view_win      = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(view_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(view_win), GTK_WIDGET(image));
    
    gtk_box_pack_start(GTK_BOX (vbox), menubar, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX (vbox), GTK_WIDGET(hbox), TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX (hbox), vthumbbox, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX (hbox), GTK_WIDGET(view_win), TRUE, TRUE, 0);

    gtk_widget_show_all(window);
    
    if(argc > 1){
        realpath(argv[argc-1], filearg); /* ty gmn and GNU info */
        if(g_file_test(filearg, G_FILE_TEST_IS_DIR))
            monocle_thumbpane_add_folder(thumbpane, filearg, recursive_load);
        else
            monocle_thumbpane_add_image(thumbpane, filearg);
    }

    /* Run Gtk */
    gtk_main();
    
    return EXIT_SUCCESS;
}
