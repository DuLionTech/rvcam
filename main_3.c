#include <gst/gst.h>

#define SOURCE_URI "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm"

typedef struct {
    GstElement *pipeline;
    GstElement *source;
    GstElement *convert;
    GstElement *resample;
    GstElement *sink;
} StreamData;

static void pad_added_handler(GstElement *src, GstPad *pad, const StreamData *sd);

int main(int argc, char *argv[]) {
    // StreamData data;
    GstBus *bus;
    GstMessage *msg;
    GstStateChangeReturn ret;
    gboolean done = FALSE;

    gst_init(&argc, &argv);

    StreamData data = {
        gst_pipeline_new("pipeline"),
        gst_element_factory_make("uridecodebin", "source"),
        gst_element_factory_make("audioconvert", "convert"),
        gst_element_factory_make("audioresample", "resample"),
        gst_element_factory_make("autoaudiosink", "sink"),
    };
    if (!data.pipeline || !data.source || !data.convert || !data.resample) {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }

    gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.convert, data.resample, data.sink, NULL);
    if (!gst_element_link_many(data.convert, data.resample, data.sink, NULL)) {
        g_printerr("Elements could not be linked.\n");
        return -1;
    }

    g_object_set(data.source, "uri", "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);
    g_signal_connect_data(data.source, "pad-added", G_CALLBACK(pad_added_handler), &data, NULL, (GConnectFlags) 0);

    g_print("Setting state to running");
    ret = gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    bus = gst_element_get_bus(data.pipeline);
    do {
        msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                         GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_STATE_CHANGED);
        if (msg != NULL) {
            GError *err;
            gchar *info;

            switch (GST_MESSAGE_TYPE(msg)) {
                case GST_MESSAGE_ERROR:
                    gst_message_parse_error(msg, &err, &info);
                    g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
                    g_printerr("Debugging information: %s\n", info ? info : "none");
                    g_clear_error(&err);
                    g_free(info);
                    done = TRUE;
                    break;
                case GST_MESSAGE_EOS:
                    g_print("End of stream reached.\n");
                    done = TRUE;
                    break;
                case GST_MESSAGE_STATE_CHANGED:
                    if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data.pipeline)) {
                        GstState old_state, new_state, pending_state;
                        gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                        g_print(
                            "Pipeline state changed from %s to %s\n",
                            gst_element_state_get_name(old_state),
                            gst_element_state_get_name(new_state));
                    }
                    break;
                default:
                    g_printerr("Unexpected message received.\n");
                    break;
            }
            gst_message_unref(msg);
        }
    } while (!done);

    return 0;
}

static void pad_added_handler(GstElement *src, GstPad *pad, const StreamData *sd) {
    GstPad *sink_pad = gst_element_get_static_pad(sd->convert, "sink");
    GstPadLinkReturn ret;
    GstCaps *pad_caps = NULL;
    GstStructure *pad_struct = NULL;
    const gchar *pad_name = NULL;
    const gchar *pad_type = NULL;

    pad_name = GST_PAD_NAME(pad);
    g_print("Received new pad '%s' from '%s':\n", pad_name, GST_ELEMENT_NAME(src));
    if (gst_pad_is_linked(sink_pad)) {
        g_print("Convert sink pad already linked.\n");
        goto exit_a;
    }

    pad_caps = gst_pad_get_current_caps(pad);
    pad_struct = gst_caps_get_structure(pad_caps, 0);
    pad_type = gst_structure_get_name(pad_struct);
    g_print("Pad '%s' type '%s' has %d capabilities.\n", pad_name, pad_type, gst_caps_get_size(pad_caps));
    if (!g_str_has_prefix(pad_type, "audio/x-raw")) {
        g_print("Ignoring pad '%s' type '%s' as it is not raw audio.\n", pad_name, pad_type);
        goto exit_b;
    }

    ret = gst_pad_link(pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
        g_print("Pad '%s' type '%s' could not be linked.\n", pad_name, pad_type);
    } else {
        g_print("Pad '%s' type '%s' linked to 'convert' pad 'sink'.\n", pad_name, pad_type, GST_PAD_NAME(sink_pad));
    }

exit_a:
    gst_caps_unref(pad_caps);

exit_b:
    gst_object_unref(sink_pad);
}
