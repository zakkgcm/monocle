/* main monocle implementation */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gtk/gtk.h>

#include "monocle.h"

static GtkWidget 
*create_menubar( GtkWidget *window, GtkItemFactoryEntry *menu_items, gint nmenu_items ){
    GtkAccelGroup *accel_group = gtk_accel_group_new();
    GtkItemFactory *item_factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>", accel_group);

    gtk_item_factory_create_items(item_factory, nmenu_items, menu_items, NULL);
    gtk_window_add_accel_group(GTK_WINDOW (window), accel_group);
    
    return gtk_item_factory_get_widget(item_factory, "<main>");
}

void usage (){
    fprintf(stderr,
            "usage: %s [args] [imagefile]\n"
            "\t-R [directory]   Recursively load files from a directory\n"
            "\t-h               Show this help message\n",
            __progname
           );
    exit(EXIT_FAILURE);
}
            

int main (int argc, char *argv[]){
    GtkWidget *window, *vbox, *menubar, *image;

    int optc;
    while((optc = getopt(argc, argv, "R")) != EOF)
        switch(optc) {
            case 'R':
                printf("Loading files from %s recursively\n", argv[2]);
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
    gtk_container_border_width(GTK_CONTAINER(vbox), 1);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    
    menubar = create_menubar(window, mainmenu_items, LENGTH(mainmenu_items));
    image = gtk_image_new_from_file(argv[1]);

    gtk_box_pack_start(GTK_BOX (vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX (vbox), image, TRUE, TRUE, 0);

    gtk_widget_show_all(window);
    
    /* Run Gtk */
    gtk_main();
    
    return EXIT_SUCCESS;
}
