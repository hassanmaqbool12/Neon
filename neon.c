#include <gtk/gtk.h>
#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>

/* Globals */
static GstElement *pipeline = NULL;
void play_audio();
static void shuffle_audio();
void collect_audio_from_same_dir();
static void open_file_dialog(GtkButton *btn, gpointer data);
static void on_file_chosen(GtkNativeDialog *dialog, gint response, gpointer data);


char **g_files = NULL;
int g_files_count = 0;
long long currentON = 0;
char *fileName;

bool isFile = false;

char filePath[1024];

bool isEven;
bool isOdd;

bool LOOP;
bool SHUFFLE;

GObject *title;
GObject *pp;
GObject *scale;
GObject *thumb;
GObject *loop;
GObject *shuffle;
GObject *next;
GObject *previous;

static void pre_file() {
    if (filePath) {
        if(currentON > 0 && g_files_count >= 0) {
            currentON -= 1;
            strcpy(filePath, g_files[currentON]);
            play_audio();
        } else {
            currentON += g_files_count - 1;
            strcpy(filePath, g_files[currentON]);
            play_audio();
        }
    }
}

bool is_file(char *path) {
    if(access(path, F_OK) == 0) {return true;}
    return false ;
}

static void next_file() {
    if (!filePath) return;
    if (g_files_count == 0) return;

    currentON = (currentON + 1) % g_files_count;
    if (currentON >= g_files_count) {
        currentON = 0;
    };
    strcpy(filePath, g_files[currentON]);

    play_audio();
}

static void on_loop() {
    if(SHUFFLE) return;
    if(LOOP == true) {
        LOOP = false;
        gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(loop)), "locked");
        //Unlock button from css active state
    } else {
        LOOP = true;
        gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(loop)), "locked");
        //Lock Button at active css state
    }
}

static void open_menu() {}

static void shuffle_playlist() {
    if(LOOP) return;
    if(SHUFFLE == true) {
        SHUFFLE = false;
        gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(shuffle)), "locked");
        //Unlock button from css active state
    } else {
        SHUFFLE = true;
        gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(shuffle)), "locked");
        //Lock Button at active css state
    }
}

void streamEND() {
    if(LOOP == true) {
        play_audio();
    } else if(SHUFFLE == true) {
        shuffle_audio();
    }else {
        next_file();
    }
}

static void shuffle_audio() {
    if(currentON + 2 > g_files_count) {
        currentON = 0;
    } else {
        currentON += 2;
        strncpy(filePath, g_files[currentON], sizeof(filePath));
        filePath[sizeof(filePath)-1] = '\0';
    }
    play_audio();
}

gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("End of stream\n");
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
            pipeline = NULL;
            streamEND();
            break;
        case GST_MESSAGE_ERROR:
            GError *err;
            gchar *debug;
            gst_message_parse_error(msg, &err, &debug);
            g_printerr("ERROR %s\n", err->message);
            g_printerr("ERROR %s\n", debug);
            g_error_free(err);
            g_free(debug);
            g_print("Error\n");
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
            pipeline = NULL;
            break;
        default:
            break;
    }
    return TRUE;
}

void stop_audio() {
    if(pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = NULL;
    }
}

static int has_ext(const char *name, const char *ext) {
    size_t ln = strlen(name);
    size_t le = strlen(ext);
    if (ln < le) return 0;
    return strcmp(name + ln - le, ext) == 0;
}

void collect_audio_from_same_dir(const char *filepath) {
    char path[512];
    strncpy(path, filepath, sizeof(path));
    path[sizeof(path)-1] = 0;

    char *dir = dirname(path);

    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(fullpath, &st) == -1) continue;
        if (!S_ISREG(st.st_mode)) continue; // skip non-files

        if (
            has_ext(ent->d_name, ".mp3") ||
            has_ext(ent->d_name, ".wav") ||
            has_ext(ent->d_name, ".ogg") ||
            has_ext(ent->d_name, ".wav")
        ) {
            // grow global list
            char **tmp = realloc(g_files, sizeof(char*) * (g_files_count + 1));
            if (!tmp) break;
            g_files = tmp;

            g_files[g_files_count] = strdup(fullpath); // store full path now
            g_files_count++;
        }
    }
    printf("[LOG] NEON: %u FILES FOUND\n", g_files_count);

    closedir(d);
}

static void
on_scale_value_changed(GtkRange *range, gpointer data)
{
    if (!pipeline) return;

    gdouble percent = gtk_range_get_value(range); // 0–100
    gint64 duration = GST_CLOCK_TIME_NONE;

    if (!gst_element_query_duration(pipeline, GST_FORMAT_TIME, &duration))
        return;

    if (duration <= 0) return;

    gint64 seek_pos = (gint64)((percent / 100.0) * duration);

    gst_element_seek_simple(
        pipeline,
        GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
        seek_pos
    );
}

static gboolean update_scale(gpointer data)
{
    if (!pipeline) return G_SOURCE_CONTINUE;

    gint64 pos = GST_CLOCK_TIME_NONE;
    gint64 dur = GST_CLOCK_TIME_NONE;

    if (!gst_element_query_position(pipeline, GST_FORMAT_TIME, &pos))
        return G_SOURCE_CONTINUE;

    if (!gst_element_query_duration(pipeline, GST_FORMAT_TIME, &dur))
        return G_SOURCE_CONTINUE;

    if (dur <= 0) return G_SOURCE_CONTINUE;

    gdouble percent = ((gdouble)pos / (gdouble)dur) * 100.0;

    // Prevent recursion (changing scale triggers seek)
    g_signal_handlers_block_by_func(scale, on_scale_value_changed, NULL);
    gtk_range_set_value(GTK_RANGE(scale), percent);
    g_signal_handlers_unblock_by_func(scale, on_scale_value_changed, NULL);

    return G_SOURCE_CONTINUE;
}



/* ============================================================
 * Play/pause
 * ============================================================*/

 static void on_click_of_pp() {
    if (!pipeline) return;

    GstState st;
    gst_element_get_state(pipeline, &st, NULL, 0);

    if (st == GST_STATE_PLAYING) {
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
        gtk_button_set_icon_name(GTK_BUTTON(pp), "media-playback-start-symbolic");
        gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(thumb)), "loop");
        gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(scale)), "blow");
    } else {
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(thumb)), "loop");
        gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(scale)), "blow");
        gtk_button_set_icon_name(GTK_BUTTON(pp), "media-playback-pause-symbolic");
    }
 }

static void
on_play_button_clicked(GtkButton *btn, gpointer data)
{   
    if(!pipeline) {open_file_dialog(btn, data);}
    on_click_of_pp();
}

/* ============================================================
 * App shutdown
 * ============================================================*/
static void
on_app_shutdown(GtkApplication *app, gpointer data)
{
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = NULL;
    }
}

static void
open_file_dialog(GtkButton *btn, gpointer data)
{

    GtkFileChooserNative *dialog =
        gtk_file_chooser_native_new(
            "Open Audio File",
            NULL,
            GTK_FILE_CHOOSER_ACTION_OPEN,
            "Open",
            "Cancel"
        );

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Audio Files");
    gtk_file_filter_add_mime_type(filter, "audio/*");

    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    g_signal_connect(
        dialog,
        "response",
        G_CALLBACK(on_file_chosen),
        NULL
    );

    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

static void
on_file_chosen(GtkNativeDialog *dialog, gint response, gpointer data)
{
    if (response != GTK_RESPONSE_ACCEPT) {
        g_object_unref(dialog);
        return;
    }

    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    GFile *file = gtk_file_chooser_get_file(chooser);
    if (!file) {
        g_object_unref(dialog);
        return;
    }

    char *path = g_file_get_path(file);
    if (!path) {
        g_object_unref(file);
        g_object_unref(dialog);
        return;
    }

    // Save selected file
    strncpy(filePath, path, sizeof(filePath));
    filePath[sizeof(filePath) - 1] = '\0';
    isFile = true;

    // Reset playlist
    for (int i = 0; i < g_files_count; i++)
        free(g_files[i]);
    free(g_files);

    g_files = NULL;
    g_files_count = 0;
    currentON = 0;

    play_audio();

    g_free(path);
    g_object_unref(file);
    g_object_unref(dialog);
}

gboolean on_key_pressed(
    GtkEventController *controller, 
    guint keyval, 
    guint keycode, 
    GdkModifierType state, 
    gpointer user_data
)
{
    if(keyval == GDK_KEY_q) {
        gtk_window_close(GTK_WINDOW(user_data));
    }

    if(keyval == GDK_KEY_l) {
        on_loop();
    }

    if(keyval == GDK_KEY_s) {
        shuffle_playlist();
    }

    if(keyval == GDK_KEY_space) {
        on_click_of_pp();
    }

    if(keyval == GDK_KEY_Left) {
        pre_file();
    }

    if(keyval == GDK_KEY_Right) {
        next_file();
    }

    if(keyval == GDK_KEY_o && (state & GDK_CONTROL_MASK)) {
         open_file_dialog(NULL, NULL);
    }

    return TRUE;
}

static void trim_name(char c)
{
    if(c && fileName) {
        char *ptr = strchr(fileName, c);
        if(ptr) 
            *ptr = '\0';
    };
    return;
}

void update_head() {
    if(fileName && title) {
        gtk_label_set_text(GTK_LABEL(title), fileName);
    } 
}

static void update_name()
{
    if(filePath) {
        g_free(fileName);
        fileName = g_path_get_basename(filePath);
        trim_name('.');
        trim_name('(');
        trim_name('_');
        trim_name('[');
        printf("[LOG] NEON: Playing: %s\n", fileName);
    }
}

void play_audio() {
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = NULL;
    }

    if (g_files_count == 0) collect_audio_from_same_dir(filePath);

    // Create playbin
    pipeline = gst_element_factory_make("playbin", "player");
    if (!pipeline) {
        g_printerr("Failed to create playbin\n");
        return;
    }

    // Convert file path to URI
    char uri[2048];
    snprintf(uri, sizeof(uri), "file://%s", filePath);
    g_object_set(pipeline, "uri", uri, NULL);

    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_call, NULL);
    gst_object_unref(bus);

    on_click_of_pp();

    gtk_range_set_value(GTK_RANGE(scale), 0);

    update_name();

    update_head();

    g_timeout_add(200, update_scale, NULL); // updates scale 5 times per sec

}


static void
activate(GtkApplication *app, gpointer data)
{
        GtkBuilder *builder = gtk_builder_new ();
        gtk_builder_add_from_file (builder, "builder.ui", NULL);

        GObject *window = gtk_builder_get_object (builder, "window");

        GtkCssProvider *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_path(provider, "style.css");

        gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(GTK_WIDGET(window)),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );

        GtkEventController *key_controller = gtk_event_controller_key_new();

        gtk_event_controller_set_propagation_phase(key_controller, GTK_PHASE_CAPTURE);
        g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), GTK_WINDOW(window));
        gtk_widget_add_controller(GTK_WIDGET(window), key_controller);

        gtk_window_set_resizable(GTK_WINDOW(window), false);
        gtk_window_set_application (GTK_WINDOW (window), app);

        GtkWidget *head = gtk_header_bar_new();

        GtkWidget *icon = gtk_image_new_from_icon_name("list-add-symbolic");
        GtkWidget *open_btn = gtk_button_new();
        gtk_button_set_child(GTK_BUTTON(open_btn), icon);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(head), open_btn);

        GtkWidget *head_title = gtk_label_new("Neon");

        gtk_header_bar_set_title_widget(GTK_HEADER_BAR(head), head_title);
        gtk_window_set_titlebar(GTK_WINDOW(window), GTK_WIDGET(head));

        g_signal_connect(
            open_btn,
            "clicked",
            G_CALLBACK(open_file_dialog),
            window
        );

        thumb = gtk_builder_get_object(builder, "player");
        title = gtk_builder_get_object(builder, "track-name");
        pp = gtk_builder_get_object(builder, "pp");
        next = gtk_builder_get_object(builder, "next");
        previous = gtk_builder_get_object(builder, "previous");
        loop = gtk_builder_get_object(builder, "loop");
        shuffle = gtk_builder_get_object(builder, "shuffle");
        scale = gtk_builder_get_object(builder, "scale");

        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(title), 18);
        g_signal_connect(pp, "clicked", G_CALLBACK(on_play_button_clicked), NULL);
        g_signal_connect(next, "clicked", G_CALLBACK(next_file), NULL);
        g_signal_connect(shuffle, "clicked", G_CALLBACK(shuffle_playlist), NULL);
        g_signal_connect(scale, "value-changed", G_CALLBACK(on_scale_value_changed), NULL);
        g_signal_connect(previous, "clicked", G_CALLBACK(pre_file), NULL);
        g_signal_connect(loop, "clicked", G_CALLBACK(on_loop), NULL);

        gtk_widget_set_visible (GTK_WIDGET (window), TRUE);
        
        if(filePath && isFile == true) {play_audio();}

        /* We do not need the builder any more */
        g_object_unref (builder);
        g_object_unref (provider);
}


// Main
 
int main(int argc, char **argv)
{
    gst_init(&argc, &argv);

    GtkApplication *app = gtk_application_new("org.hassan.music", G_APPLICATION_FLAGS_NONE);

    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_app_shutdown), NULL);

    // If a file path was passed via command line, store it
    if(argc > 1 && is_file(argv[1]) == true) {
        strncpy(filePath, argv[1], sizeof(filePath));
        filePath[sizeof(filePath)-1] = '\0';
        isFile = true;

        // Remove file from argv so GtkApplication ignores it
        argc--;
        for(int i = 1; i < argc; i++)
            argv[i] = argv[i+1];
    }

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);

    // Free the global file list
    for(int i = 0; i < g_files_count; i++)
        free(g_files[i]);
    free(g_files);

    return status;
}