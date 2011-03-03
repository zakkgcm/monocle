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
GtkUIManager *uimanager;
GtkWidget *window, *vbox, *hbox, *vthumbbox, *hthumbbox;

struct _settings {
    gboolean autohide_thumbpane;
}; 
static struct _settings settings;

/* loads the config file
 * warn the user then move along if something goes wrong
 * i'd imagine there's a cleaner way to do this 
 */
static void
load_config () {
    GKeyFile *config;
    gchar *config_file;

    gint conf_threads;
    gboolean conf_scalegifs;

    GError *error = NULL;

    config_file = g_build_filename(g_get_user_config_dir(), "monocle/", "monocle.conf", NULL);
    
    if (g_file_test(config_file, G_FILE_TEST_EXISTS)) {
        config = g_key_file_new();
        
        if (!g_key_file_load_from_file(config, config_file, G_KEY_FILE_NONE, &error)) {
            /* something went wrong besides there not being a config file */
            if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
                printf("[monocle] problem when parsing config: %s\n", error->message);

            g_clear_error(&error);
            return;
        }

        /* grab necessary keys and load them into appropriate widgets */
        /* this is going to get real messy real fast */
        if ((conf_threads = g_key_file_get_integer(config, "monocle", "threads", &error)) <= 0) {
            conf_threads = 1;
            g_clear_error(&error);
        }
        monocle_thumbpane_set_num_threads(thumbpane, conf_threads);

        conf_scalegifs = g_key_file_get_boolean(config, "monocle", "scalegifs", &error);
        g_clear_error(&error);
        monocle_view_set_scale_gifs(image, conf_scalegifs);

        settings.autohide_thumbpane = g_key_file_get_boolean(config, "thumbpane", "autohide", &error);
        g_clear_error(&error);

        g_key_file_free(config);

        
    }

    /* this would be cleaner but involve a lot of string comparison */
    /*if ((config_keys = g_key_file_get_keys(config, "monocle", NULL, error)) == NULL) {
        printf("[[monocle] problem when loading config: %s\n", error->message);
        g_clear_error(error);
    }

    for (i = 0; i <= LENGTH(config_keys); i++) {
        if (strcmp(config_keys[i], "scale") == 0) {
            monocle_view_set_scale(image, g_key_file_get_double (config, "monocle", config_keys[i], error));
        }
    }*/

    g_free(config_file);
}

/* saves the config file */
static void
save_config () {
    GKeyFile *config;

    gchar *config_dir;
    gchar *config_file;
    gchar *config_buf;

    FILE *config_fd;
    int bytes_written;

    config_dir = g_build_filename(g_get_user_config_dir(), "monocle/", NULL);
    g_mkdir_with_parents(config_dir, 0755);

    config_file = g_build_filename(config_dir, "monocle.conf", NULL);
    config = g_key_file_new();

    g_key_file_set_integer(config, "monocle", "threads", monocle_thumbpane_get_num_threads(thumbpane));
    g_key_file_set_boolean(config, "monocle", "scalegifs", monocle_view_get_scale_gifs(image));
    g_key_file_set_boolean(config, "thumbpane", "autohide", settings.autohide_thumbpane);

    config_buf = g_key_file_to_data(config, NULL, NULL);
    
    if ((config_fd = fopen(config_file, "w")) == NULL) {
        printf("failed to write config file at %s\n", config_file);
    } else {
        bytes_written = fwrite(config_buf, sizeof(char), strlen(config_buf), config_fd);
        if (bytes_written != strlen(config_buf))
            printf("error writing to config file at %s\n", config_file);

        fclose(config_fd);
    }

    g_free(config_buf);
    g_free(config_file);
    g_free(config_dir);
}

static void
cb_set_image (GtkWidget *widget, gchar *filename, gpointer data) {
    /* what am I even DOING this is absurd */
    gchar *newtitle;
    
    if (filename != NULL) {
        newtitle = g_malloc(strlen(filename) + 11);
        sprintf(newtitle, "%s - Monocle", filename);
        gtk_window_set_title(GTK_WINDOW(window), (const gchar *) newtitle);
        g_free(newtitle);
    } else {
        gtk_window_set_title(GTK_WINDOW(window), "NOTHING - Monocle");
    }

    monocle_view_set_image(image, filename);
}

static void
cb_rowcount_changed (GtkWidget *widget, gint rowcount, gpointer data) {
    if (rowcount <= 1 && settings.autohide_thumbpane)
        gtk_widget_hide(vthumbbox);
    else if (!gtk_widget_get_visible(vthumbbox))
        gtk_widget_show(vthumbbox);
        
}

/* Menu Actions */
static void
action_open_file (gpointer callback_data, guint callback_action, GtkWidget *menu_item) {
    GtkWidget *chooser = gtk_file_chooser_dialog_new("Open Image(s)", GTK_WINDOW(window), 
                                GTK_FILE_CHOOSER_ACTION_OPEN,
                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				                GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				                NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), TRUE);

    if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT) { 
        GSList *files;
        files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER (chooser)); /*get uris doesn't encode right for thumbnails*/
        monocle_thumbpane_add_many(thumbpane, files);
        g_slist_free (files);
    }
    gtk_widget_destroy (chooser);
    
    return;
}

static void
action_open_folder (gpointer callback_data, guint callback_action, GtkWidget *menu_item) {
    GtkWidget *recursive_toggle = gtk_check_button_new_with_label("Open Recursively?");
    GtkWidget *chooser = gtk_file_chooser_dialog_new("Open Folder", GTK_WINDOW(window), 
                                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				                GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				                NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), TRUE);
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(chooser), recursive_toggle);
    
    if (gtk_dialog_run (GTK_DIALOG (chooser)) == GTK_RESPONSE_ACCEPT) { 
        gchar *folder;
        gboolean recursive_load = FALSE;
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(recursive_toggle)))
            recursive_load = TRUE;
        folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (chooser));
        monocle_thumbpane_add_folder(thumbpane, folder, recursive_load);
        g_free (folder);
    }
    gtk_widget_destroy (chooser);
    
    return;
}

static void
action_edit_preferences (gpointer callback_data, guint callback_action, GtkWidget *menu_item) {
    GtkWidget *preferences, *content_area, *table, *spin_threads, *check_scalegifs, *check_autohide_thumbpane;
    preferences = gtk_dialog_new_with_buttons("Monocle Preferences", GTK_WINDOW(window),
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_CLOSE, NULL);

    content_area = gtk_dialog_get_content_area (GTK_DIALOG(preferences));
    table = gtk_table_new(10, 2, FALSE);

    spin_threads = gtk_spin_button_new_with_range (1, 1000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_threads), (gdouble)monocle_thumbpane_get_num_threads(thumbpane));

    check_scalegifs = gtk_check_button_new_with_label ("Scale Animations?");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_scalegifs), monocle_view_get_scale_gifs(image));

    check_autohide_thumbpane = gtk_check_button_new_with_label ("Autohide_Thumbpane?");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_autohide_thumbpane), settings.autohide_thumbpane);
    
    gtk_table_attach(GTK_TABLE(table), gtk_label_new("Thumbnailing Threads"), 0, 1, 0, 1, GTK_EXPAND|GTK_FILL, GTK_SHRINK, 5, 1);
    gtk_table_attach(GTK_TABLE(table), spin_threads, 1, 2, 0, 1, GTK_SHRINK, GTK_EXPAND|GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), check_scalegifs, 0, 1, 2, 3, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), check_autohide_thumbpane, 0, 1, 3, 4, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
   
    gtk_container_add(GTK_CONTAINER(content_area), table);
    gtk_widget_show_all(content_area);
    
    gtk_dialog_run(GTK_DIALOG(preferences));

    monocle_thumbpane_set_num_threads(thumbpane, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_threads)));
    monocle_view_set_scale_gifs(image, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_scalegifs)));
    settings.autohide_thumbpane = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_autohide_thumbpane));

    gtk_widget_destroy(preferences);
    return;
}

static void 
action_scale_menu (GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
    switch (gtk_radio_action_get_current_value(current)) {
        case 2:
            monocle_view_set_zoom_mode(image, MONOCLE_SCALE_FITHEIGHT);
            break;
        case 3:
            monocle_view_set_zoom_mode(image, MONOCLE_SCALE_FITWIDTH);
            break;
        case 4:
            monocle_view_set_zoom_mode(image, MONOCLE_SCALE_CUSTOM);
            break;
        case 1:
        default:
            monocle_view_set_zoom_mode(image, MONOCLE_SCALE_1TO1);
    }
    return;
}

static void
action_zoom_in () {
    monocle_view_set_scale(image, monocle_view_get_scale(image) * 1.25);
}

static void
action_zoom_out () {
    monocle_view_set_scale(image, monocle_view_get_scale(image) * 0.8);
}

static void
action_sort_menu (GtkRadioAction *action, GtkRadioAction *current, gpointer user_data) {
    switch (gtk_radio_action_get_current_value(current)) {
        case -1:
            monocle_thumbpane_sort_order_ascending (thumbpane);
            break;
        case -2:
            monocle_thumbpane_sort_order_descending (thumbpane);
            break;
        case 1:
            monocle_thumbpane_sort_by_name (thumbpane);
            break;
        case 2:
            monocle_thumbpane_sort_by_date (thumbpane);
            break;
        case 3:
            monocle_thumbpane_sort_by_size (thumbpane);
            break;
    }
    return;
}

/* Button Callbacks */
static gboolean
cb_thumbpane_addrmbutton (GtkWidget *button, GdkEventButton *event, gpointer user_data) {
    if (user_data == 0) {
        monocle_thumbpane_remove_current(thumbpane);
    }
    return FALSE;
}

static void usage () {
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

/* returns true so we can save the config */
/* runs twice for some reason */
static gboolean monocle_quit () {
    save_config();
    gtk_main_quit();
    return TRUE;
}

/* Main Loop */
int main (int argc, char *argv[]) {
    GtkWidget *view_win,
              *thumbadd, *thumbrm;
    GtkActionGroup *action_group;
    GError *error = NULL;

    gchar filearg[PATH_MAX+1];
    gfloat scale = 1.0;
    MonocleZoomMode zoom_mode = MONOCLE_SCALE_FITHEIGHT;
    gboolean recursive_load = FALSE;

    /* Parse Arguments */
    int optc;
    extern char *optarg;
    while ((optc = getopt(argc, argv, "hvRs:")) != EOF)
        switch (optc) {
            case 'R':
                recursive_load = TRUE;
                break;
            case 's':
                if (!strcmp(optarg, "fitheight")) {
                    zoom_mode = MONOCLE_SCALE_FITHEIGHT;
                    printf("Setting scale to fit to height\n");
                }else if (!strcmp(optarg, "fitwidth")) {
                    zoom_mode = MONOCLE_SCALE_FITWIDTH;
                    printf("Setting scale to fit to width\n");
                }else if (atof(optarg) > 0) { 
                    zoom_mode = MONOCLE_SCALE_CUSTOM;
                    scale = atof(optarg);
                } else {
                    printf("Unknown scale %s, defaulting to fit to height\n", optarg);
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
    g_thread_init(NULL);
    gdk_threads_init();

    /* UI Manager Setup */
    uimanager = gtk_ui_manager_new();
    action_group = gtk_action_group_new ("Menu Actions");

    gtk_action_group_add_actions (action_group, main_entries, G_N_ELEMENTS(main_entries), NULL);
    gtk_action_group_add_radio_actions (action_group, zoom_entries, G_N_ELEMENTS(zoom_entries), 2,
                                                                    G_CALLBACK(action_scale_menu), NULL);
    gtk_action_group_add_actions (action_group, zoom_inout_entries, G_N_ELEMENTS(zoom_inout_entries), NULL);
    gtk_action_group_add_radio_actions (action_group, sorttype_entries, G_N_ELEMENTS(sorttype_entries), 1,
                                                                        G_CALLBACK(action_sort_menu), NULL);
    gtk_action_group_add_radio_actions (action_group, sortdirection_entries, G_N_ELEMENTS(sortdirection_entries), -1,
                                                                             G_CALLBACK(action_sort_menu), NULL);
    gtk_ui_manager_insert_action_group (uimanager, action_group, 0);

    gtk_ui_manager_add_ui_from_string (uimanager, monocle_ui, -1, &error);
    if (error) {
        printf("[monocle] problem when parsing menus: %s\n", error->message);
        g_error_free(error);
        exit(EXIT_FAILURE);
    }

    /* Widget Setup */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_add_accel_group(GTK_WINDOW(window), gtk_ui_manager_get_accel_group(uimanager));
    g_signal_connect(G_OBJECT(window), "delete_event", G_CALLBACK(monocle_quit), NULL);
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 500);

    /* Thumbpane VBox and Buttons */
    thumbpane = g_object_new(MONOCLE_TYPE_THUMBPANE, NULL);
    g_signal_connect(G_OBJECT(thumbpane), "image-changed", G_CALLBACK(cb_set_image), NULL);
    g_signal_connect(G_OBJECT(thumbpane), "rowcount-changed", G_CALLBACK(cb_rowcount_changed), NULL);

    vthumbbox   = gtk_vbox_new(FALSE, 1);
    hthumbbox   = gtk_hbutton_box_new();
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

    /*menubar = create_menubar(window, mainmenu_items, LENGTH(mainmenu_items));*/
    image = g_object_new(MONOCLE_TYPE_VIEW, NULL);
    monocle_view_set_scale(image, scale);
    monocle_view_set_zoom_mode(image, zoom_mode);
    
    gtk_widget_set_size_request(GTK_WIDGET(thumbpane), 150, -1);

    /* We've made our widgets, load config */
    load_config();

    /* Contain the MonocleView */
    view_win      = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(view_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(view_win), GTK_WIDGET(image));
    
    gtk_box_pack_start(GTK_BOX (vbox), gtk_ui_manager_get_widget(uimanager, "/MainMenubar"), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX (vbox), GTK_WIDGET(hbox), TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX (hbox), vthumbbox, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX (hbox), GTK_WIDGET(view_win), TRUE, TRUE, 0);
    
    gtk_widget_show_all(window);
    
    /* Program Initialization */
    if (settings.autohide_thumbpane)
        gtk_widget_hide(vthumbbox);

    if (argc > 1) {
#ifdef WIN32
        strcpy(filearg, argv[argc-1]);
#else
        realpath(argv[argc-1], filearg); /* ty gmn and GNU info */
#endif
        /* am I even supposed to wrap these in enter/leave? supposedly since they're called outside of a callback I do */
        if (g_file_test(filearg, G_FILE_TEST_IS_DIR)) {
            gdk_threads_enter();
            monocle_thumbpane_add_folder(thumbpane, filearg, recursive_load);
            gdk_threads_leave();
        } else {
            gdk_threads_enter();
            monocle_thumbpane_add_image(thumbpane, filearg);
            gdk_threads_leave();
        }
    }

    /* Run Gtk */
    gdk_threads_enter();
    gtk_main();
    gdk_threads_leave();
    
    /* Save our config before we go */
    save_config();
    return EXIT_SUCCESS;
}
