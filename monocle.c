/* main monocle implementation */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

void cb_open_file (gpointer callback_data, guint callback_action, GtkWidget *menu_item){
    GtkWidget *chooser = gtk_file_chooser_dialog_new("Open Image(s)", GTK_WINDOW(window), 
                                GTK_FILE_CHOOSER_ACTION_OPEN,
                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				                GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				                NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), TRUE);

    if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT){ 
        char *file;
        file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (chooser));
        monocle_thumbpane_add_image(thumbpane, file);
        monocle_view_set_image(image, file);
        g_free (file);
    }
    gtk_widget_destroy (chooser);
    
    return;
}

/* Menu callbacks */
void cb_scale_image (gpointer callback_data, guint callback_action, GtkWidget *menu_item){
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


void usage (){
    fprintf(stderr,
            "usage: %s [args] [imagefile]\n"
            "\t-R [directory]   Recursively load files from a directory (defunct)\n"
            "\t-s [scale]       Set the initial scale, 'fit' or 0 for fit to window (default)\n"
            "\t-h               Show this help message\n",
            __progname
           );
    exit(EXIT_FAILURE);
}
            

int main (int argc, char *argv[]){
    GtkWidget *vbox, *hbox, *menubar, *scrolledwin;
    float scale = 0;

    int optc;
    extern char *optarg;
    while((optc = getopt(argc, argv, "R:s:")) != EOF)
        switch(optc) {
            case 'R':
                printf("Loading files from %s recursively\n", optarg);
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
            default:
                usage();
                exit(EXIT_FAILURE);
        }

    /* Gtk Setup */
    gtk_init(&argc, &argv);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(G_OBJECT(window), "delete_event", G_CALLBACK(gtk_main_quit), NULL);
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 500);

    /* Main VBox */
    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_border_width(GTK_CONTAINER (vbox), 1);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    
    /* HBox, contains monocleview and thumbpane */
    hbox = gtk_hbox_new(FALSE, 1);
    gtk_container_border_width(GTK_CONTAINER (hbox), 1);

    menubar = create_menubar(window, mainmenu_items, LENGTH(mainmenu_items));
    image = g_object_new(MONOCLE_TYPE_VIEW, NULL);
    thumbpane = g_object_new(MONOCLE_TYPE_THUMBPANE, NULL);
    monocle_view_set_scale(image, scale);
    
    gtk_widget_set_size_request(GTK_WIDGET(thumbpane), 150, -1);

    /* Contain the MonocleView */
    scrolledwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolledwin), GTK_WIDGET(image));
    
    gtk_box_pack_start(GTK_BOX (vbox), menubar, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX (vbox), GTK_WIDGET(hbox), TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX (hbox), GTK_WIDGET(thumbpane), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX (hbox), GTK_WIDGET(scrolledwin), TRUE, TRUE, 0);

    gtk_widget_show_all(window);
   
    if(argc > 1)
        monocle_view_set_image(image, argv[argc-1]);

    /* Run Gtk */
    gtk_main();
    
    return EXIT_SUCCESS;
}
