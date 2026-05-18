#include <string.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gdk/gdk.h>

#include "prefix.h"

typedef struct {
    GstElement *playbin;
    GtkWidget *sink_widget;
    GtkWidget *slider;
    GtkWidget *streams_list;
    gulong slider_update_id;

    GstState state;
    gint64 duration;
} StreamData;

static void play_cb(GtkButton *button, StreamData *data);

static void pause_cb(GtkButton *button, StreamData *data);

static void stop_cb(GtkButton *button, StreamData *data);

static void delete_event_cb(GtkWidget *widget, GdkEvent *event, StreamData *data);

static void slider_cb(GtkRange *range, StreamData *data);

static void create_ui(StreamData *data);

static gboolean refresh_ui(StreamData *data);

static void tags_cb(GstElement *playbin, gint stream, StreamData *data);

static void error_cb(GstBus *bus, GstMessage *msg, StreamData *data);

static void eos_cb(GstBus *bus, GstMessage *msg, StreamData *data);

static void state_changed_cb(GstBus *bus, GstMessage *msg, StreamData *data);

static void analyze_streams(StreamData *data);

static void application_cb(GstBus *bus, GstMessage *msg, StreamData *data);

int main(int argc, char *argv[]) {
    StreamData data;
    GstStateChangeReturn ret;
    GstBus *bus;
    GstElement *gtkglsink, *videosink;

    // Initialize GTK and GStreamer
    gtk_init(&argc, &argv);
    gst_init(&argc, &argv);

    // Initialize data structure and create elements
    memset(&data, 0, sizeof(StreamData));
    data.duration = GST_CLOCK_TIME_NONE;

    videosink = gst_element_factory_make("glsinkbin", "glsinkbin");
    gtkglsink = gst_element_factory_make("gtkglsink", "gtkglsink");
    data.playbin = gst_element_factory_make("playbin", "playbin");
    if (!data.playbin) {
        g_printerr("Element 'playbin' could not be created.\n");
        return -1;
    }

    if (gtkglsink != NULL && videosink != NULL) {
        g_print("Successfully created GTK GL Sink\n");
        g_object_set(videosink, "sink", gtkglsink, NULL);
        g_object_get(gtkglsink, "widget", &data.sink_widget, NULL);
    } else {
        g_printerr("Could not create 'gtkglsink', falling back to 'gtksink'.\n");
        videosink = gst_element_factory_make("gtksink", "gtksink");
        g_object_get(videosink, "widget", &data.sink_widget, NULL);
    }

    if (!videosink) {
        g_printerr("Could not create elements 'gtkglsink' and 'gtksink'.\n");
        return -1;
    }

    g_object_set(data.playbin, "uri", SOURCE_URI, NULL);
    g_object_set(data.playbin, "video-sink", videosink, NULL);

    g_signal_connect(G_OBJECT(data.playbin), "video-tags-changed", G_CALLBACK(tags_cb), &data);
    g_signal_connect(G_OBJECT(data.playbin), "audio-tags-changed", G_CALLBACK(tags_cb), &data);
    g_signal_connect(G_OBJECT(data.playbin), "text-tags-changed", G_CALLBACK(tags_cb), &data);

    create_ui(&data);

    bus = gst_element_get_bus(data.playbin);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(G_OBJECT(bus), "message::error", G_CALLBACK(error_cb), &data);
    g_signal_connect(G_OBJECT(bus), "message::eos", G_CALLBACK(eos_cb), &data);
    g_signal_connect(G_OBJECT(bus), "message::state-changed", G_CALLBACK(state_changed_cb), &data);
    g_signal_connect(G_OBJECT(bus), "message::application", G_CALLBACK(application_cb), &data);

    // Start playing
    ret = gst_element_set_state(data.playbin, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the playing state.\n");
        GstMessage *msg = gst_bus_timed_pop_filtered(bus, 0, GST_MESSAGE_ERROR);
        if (msg != NULL) {
            error_cb(bus, msg, &data);
            gst_message_unref (msg);
        }
        gst_element_set_state(data.playbin, GST_STATE_NULL);
        gst_object_unref(data.playbin);
        return -1;
    }

    g_timeout_add_seconds(1, G_SOURCE_FUNC(refresh_ui), &data);
    gtk_main();

    gst_element_set_state(data.playbin, GST_STATE_NULL);
    gst_object_unref(data.playbin);
    return 0;
}

static void create_ui(StreamData *data) {
    GtkWidget *window;
    GtkWidget *toolbar;
    GtkWidget *child;
    GtkWidget *view;
    GtkWidget *play_button, *pause_button, *stop_button;

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(delete_event_cb), data);

    play_button = gtk_button_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_SMALL_TOOLBAR);
    g_signal_connect(play_button, "clicked", G_CALLBACK(play_cb), NULL);

    pause_button = gtk_button_new_from_icon_name("media-playback-pause", GTK_ICON_SIZE_SMALL_TOOLBAR);
    g_signal_connect(pause_button, "clicked", G_CALLBACK(pause_cb), NULL);

    stop_button = gtk_button_new_from_icon_name("media-playback-stop", GTK_ICON_SIZE_SMALL_TOOLBAR);
    g_signal_connect(stop_button, "clicked", G_CALLBACK(stop_cb), NULL);

    data->slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(data->slider), 0);
    data->slider_update_id = g_signal_connect(G_OBJECT(data->slider), "value-changed", G_CALLBACK(slider_cb), data);

    data->streams_list = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(data->streams_list), FALSE);

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), play_button, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(toolbar), pause_button, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(toolbar), stop_button, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(toolbar), data->slider, TRUE, TRUE, 2);

    view = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(view), data->sink_widget, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(view), data->streams_list, FALSE, FALSE, 2);

    child = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(child), view, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(child), toolbar, FALSE, FALSE, 2);
    gtk_container_add(GTK_CONTAINER(window), child);
    gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);

    gtk_widget_show_all(window);
}

static void play_cb(GtkButton *button, StreamData *data) {
    g_print("Playing");
    gst_element_set_state(data->playbin, GST_STATE_PLAYING);
}

static void pause_cb(GtkButton *button, StreamData *data) {
    g_print("Paused");
    gst_element_set_state(data->playbin, GST_STATE_PAUSED);
}

static void stop_cb(GtkButton *button, StreamData *data) {
    g_print("Ready");
    gst_element_set_state(data->playbin, GST_STATE_READY);
}

static void delete_event_cb(GtkWidget *widget, GdkEvent *event, StreamData *data) {
    g_print("Delete");
    stop_cb(NULL, data);
    gtk_main_quit();
}

static void slider_cb(GtkRange *range, StreamData *data) {
    gdouble value = gtk_range_get_value(GTK_RANGE(data->slider));
    gst_element_seek_simple(
        data->playbin,
        GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
        (gint64) (value * GST_SECOND));
}

// ReSharper disable once CppDFAConstantFunctionResult
static gboolean refresh_ui(StreamData *data) {
    gint64 current = -1;

    if (data->state < GST_STATE_PAUSED) {
        return TRUE;
    }

    if (!GST_CLOCK_TIME_IS_VALID(data->duration)) {
        if (!gst_element_query_duration(data->playbin, GST_FORMAT_TIME, &data->duration)) {
            g_printerr("Could not query current duration.\n");
        } else {
            gtk_range_set_range(GTK_RANGE(data->slider), 0, (gdouble) data->duration / GST_SECOND);
        }
    }

    if (gst_element_query_position(data->playbin, GST_FORMAT_TIME, &current)) {
        g_signal_handler_block(data->slider, data->slider_update_id);
        gtk_range_set_value(GTK_RANGE(data->slider), (gdouble) current / GST_SECOND);
        g_signal_handler_unblock(data->slider, data->slider_update_id);
    }

    return TRUE;
}

static void tags_cb(GstElement *playbin, gint stream, StreamData *data) {
    gst_element_post_message(
        playbin,
        gst_message_new_application(
            GST_OBJECT(playbin),
            gst_structure_new_empty("tags-changed")));
}

static void error_cb(GstBus *bus, GstMessage *msg, StreamData *data) {
    GError *err;
    gchar *info;

    gst_message_parse_error(msg, &err, &info);
    g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
    g_printerr("Debugging information: %s\n", info ? info : "none");
    g_clear_error(&err);
    g_free(info);
    gst_element_set_state(data->playbin, GST_STATE_READY);
}

static void eos_cb(GstBus *bus, GstMessage *msg, StreamData *data) {
    g_print("End-Of-Stream reached.\n");
    gst_element_set_state(data->playbin, GST_STATE_READY);
}

static void state_changed_cb(GstBus *bus, GstMessage *msg, StreamData *data) {
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
    if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data->playbin)) {
        data->state = new_state;
        g_print(
            "Pipeline state changed from %s to %s\n",
            gst_element_state_get_name(old_state),
            gst_element_state_get_name(new_state));
        if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
            refresh_ui(data);
        }
    }
}

static void analyze_streams(StreamData *data) {
    gint i;
    GstTagList *tags;
    gchar *str, *fmt;
    guint rate;
    gint n_video, n_audio, n_text;
    GtkTextBuffer *text;

    text = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data->streams_list));
    gtk_text_buffer_set_text(text, "", -1);

    g_object_get(data->playbin, "n-video", &n_video, NULL);
    g_object_get(data->playbin, "n-audio", &n_audio, NULL);
    g_object_get(data->playbin, "n-text", &n_text, NULL);

    for (i = 0; i < n_video; i++) {
        tags = NULL;
        g_signal_emit_by_name(data->playbin, "get-video-tags", i, &tags);
        if (tags) {
            fmt = g_strdup_printf("video stream %d:\n", i);
            gtk_text_buffer_insert_at_cursor(text, fmt, -1);
            g_free(fmt);

            gst_tag_list_get_string(tags, GST_TAG_VIDEO_CODEC, &str);
            fmt = g_strdup_printf("  codec: %s\n\n", str ? str : "unknown");
            gtk_text_buffer_insert_at_cursor(text, fmt, -1);
            g_free(fmt);
            g_free(str);
            gst_tag_list_free(tags);
        }
    }

    for (i = 0; i < n_audio; i++) {
        tags = NULL;
        g_signal_emit_by_name(data->playbin, "get-audio-tags", i, &tags);
        if (tags) {
            fmt = g_strdup_printf("audio stream %d:\n", i);
            gtk_text_buffer_insert_at_cursor(text, fmt, -1);
            g_free(fmt);

            if (gst_tag_list_get_string(tags, GST_TAG_AUDIO_CODEC, &str)) {
                fmt = g_strdup_printf("  codec: %s\n", str);
                gtk_text_buffer_insert_at_cursor(text, fmt, -1);
                g_free(fmt);
                g_free(str);
            }

            if (gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &str)) {
                fmt = g_strdup_printf("  language: %s\n", str);
                gtk_text_buffer_insert_at_cursor(text, fmt, -1);
                g_free(fmt);
                g_free(str);
            }

            if (gst_tag_list_get_uint(tags, GST_TAG_BITRATE, &rate)) {
                fmt = g_strdup_printf("  bitrate: %d\n\n", rate);
                gtk_text_buffer_insert_at_cursor(text, fmt, -1);
                g_free(fmt);
            }
            gst_tag_list_free(tags);
        }
    }

    for (i = 0; i < n_text; i++) {
        tags = NULL;
        g_signal_emit_by_name(data->playbin, "get-text-tags", i, &tags);
        if (tags) {
            fmt = g_strdup_printf("subtitle stream %d:\n", i);
            gtk_text_buffer_insert_at_cursor(text, fmt, -1);
            g_free(fmt);

            if (gst_tag_list_get_string(tags, GST_TAG_LANGUAGE_CODE, &str)) {
                fmt = g_strdup_printf("  language: %s\n", str);
                gtk_text_buffer_insert_at_cursor(text, fmt, -1);
                g_free(fmt);
                g_free(str);
            }
            gst_tag_list_free(tags);
        }
    }
}

static void application_cb(GstBus *bus, GstMessage *msg, StreamData *data) {
    if (g_strcmp0(gst_structure_get_name(gst_message_get_structure(msg)), "tags-changed") == 0) {
        analyze_streams(data);
    }
}
