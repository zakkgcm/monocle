/* main monocle implementation */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gtk/gtk.h>

#include "monocle.h"
#include "monocleview.h"

MonocleView *image;

static GtkWidget 
*create_menubar( GtkWidget *window, GtkItemFactoryEntry *menu_items, gint nmenu_items ){
    GtkAccelGroup *accel_group = gtk_accel_group_new();
    GtkItemFactory *item_factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>", accel_group);

    gtk_item_factory_create_items(item_factory, nmenu_items, menu_items, NULL);
    gtk_window_add_accel_group(GTK_WINDOW (window), accel_group);
    
    return gtk_item_factory_get_widget(item_factory, "<main>");
}

/* Menu callbacks */
void scale_image_cb (gpointer callback_data, guint callback_action, GtkWidget *menu_item){
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
            "\t-R [directory]   Recursively load files from a directory\n"
            "\t-s [scale]       Set the initial scale, 'fit' or 0 for fit to window (default)\n"
            "\t-h               Show this help message\n",
            __progname
           );
    exit(EXIT_FAILURE);
}
            

int main (int argc, char *argv[]){
    GtkWidget *window, *vbox, *menubar;
    float scale = 0;

    int optc;
    while((optc = getopt(argc, argv, "Rs:")) != EOF)
        switch(optc) {
            case 'R':
                printf("Loading files from %s recursively\n", optarg);
                break;
            case 's':
                if(strcmp(optarg, "fit"))
                    scale = 0;
                else if((float)atof(optarg) > 0.0) 
                    scale = (float)atof(optarg);

                printf("Setting scale to %.1f\n", scale);
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

    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_border_width(GTK_CONTAINER (vbox), 1);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    
    menubar = create_menubar(window, mainmenu_items, LENGTH(mainmenu_items));
    image = g_object_new(MONOCLE_TYPE_VIEW, NULL);

    gtk_box_pack_start(GTK_BOX (vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX (vbox), GTK_WIDGET(image), TRUE, TRUE, 0);

    gtk_widget_show_all(window);
    
    monocle_view_set_image(image, argv[1]);
    /* Run Gtk */
    gtk_main();
    
    return EXIT_SUCCESS;
}
