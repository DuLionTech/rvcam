#include <gst/gst.h>
#include <string.h>
#include "prefix.h"

typedef struct {
    gboolean is_live;
    GstElement *pipeline;
    GMainLoop *loop;
} StreamData;

static void cb_message(GstBus *bus, GstMessage *msg, StreamData *data);

int main(int argc, char *argv[]) {
    GstElement *pipeline;
    GstBus *bus;
    GstStateChangeReturn ret;
    GMainLoop *loop;
    StreamData data;

    gst_init(&argc, &argv);

    memset(&data, 0, sizeof(StreamData));
    pipeline = gst_parse_launch("playbin uri=" SOURCE_URI, NULL);
    bus = gst_element_get_bus(pipeline);

    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        gst_object_unref(pipeline);
        return -1;
    }
    if (ret == GST_STATE_CHANGE_NO_PREROLL) {
        data.is_live = TRUE;
    }

    loop = g_main_loop_new(NULL, FALSE);
    data.loop = loop;
    data.pipeline = pipeline;

    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message", G_CALLBACK(cb_message), &data);
    g_main_loop_run(loop);

    g_main_loop_run(loop);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return 0;
}

static void cb_message(GstBus *bus, GstMessage *msg, StreamData *data) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;

            gst_message_parse_error(msg, &err, &debug);
            g_printerr("ERROR: %s\n", err->message);
            g_error_free(err);
            g_free(debug);

            gst_element_set_state(data->pipeline, GST_STATE_READY);
            g_main_loop_quit(data->loop);
            break;
        }
        case GST_MESSAGE_EOS:
            gst_element_set_state(data->pipeline, GST_STATE_READY);
            g_main_loop_quit(data->loop);
            break;
        case GST_MESSAGE_BUFFERING: {
            gint percent = 0;
            if (data->is_live) break;
            gst_message_parse_buffering(msg, &percent);
            g_print("Buffering percent: %3d%\n", percent);
            if (percent < 100) {
                gst_element_set_state(data->pipeline, GST_STATE_PAUSED);
            } else {
                gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
            }
            break;
        }
        case GST_MESSAGE_CLOCK_LOST:
            gst_element_set_state(data->pipeline, GST_STATE_PAUSED);
            gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
            break;
        default:
            break;
    }
}
