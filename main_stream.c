#include <gst/gst.h>

#define RTSP_LOCATION "rtsp://192.168.16.128:554/stream1"

typedef struct {
    GstElement *source;
    GstElement *depay;
    GstElement *parse;
    GstElement *decode;
    GstElement *convert;
    GstElement *sink;
    GstElement *pipeline;
    GMainLoop *loop;
} StreamData;

static gboolean select_stream(GstElement *src, guint num, GstCaps *caps, StreamData *data);

static void pad_added(GstElement *src, GstPad *src_pad, const StreamData *data);

static void error_cb(GstBus *bus, GstMessage *msg, const StreamData *data);

int main(int argc, char *argv[]) {
    StreamData data = {0};
    GstBus *bus;
    GstStateChangeReturn ret;

    gst_init(&argc, &argv);
    data.pipeline = gst_pipeline_new("rtsp-client");
    data.source = gst_element_factory_make("rtspsrc", "source");
    data.depay = gst_element_factory_make("rtph265depay", "extract");
    data.parse = gst_element_factory_make("h265parse", "parse");
    data.decode = gst_element_factory_make("avdec_h265", "decode");
    data.convert = gst_element_factory_make("videoconvert", "convert");
    data.sink = gst_element_factory_make("autovideosink", "sink");
    if (!data.pipeline ||
        !data.source ||
        !data.depay ||
        !data.parse ||
        !data.decode ||
        !data.convert ||
        !data.sink) {
        g_printerr("Not all element could be created.\n");
        return -1;
    }
    gst_bin_add_many(
        GST_BIN(data.pipeline),
        data.source,
        data.depay,
        data.parse,
        data.decode,
        data.convert,
        data.sink,
        NULL);
    if (!gst_element_link(data.depay, data.parse)) {
        g_printerr("Could not link depay to parse");
        return -1;
    }
    if (!gst_element_link(data.parse, data.decode)) {
        g_printerr("Could not link parse to decode");
        return -1;
    }
    if (!gst_element_link(data.decode, data.convert)) {
        g_printerr("Could not link decode to convert");
        return -1;
    }
    if (!gst_element_link(data.convert, data.sink)) {
        g_printerr("Could not link convert to sink");
        return -1;
    }

    bus = gst_element_get_bus(data.pipeline);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(G_OBJECT(bus), "message::error", G_CALLBACK(error_cb), NULL);
    gst_object_unref(bus);

    g_object_set(data.source, "location", RTSP_LOCATION, NULL);
    g_object_set(data.source, "latency", 100, NULL);
    g_signal_connect(data.source, "pad-added", G_CALLBACK(pad_added), &data);
    g_signal_connect(data.source, "select-stream", G_CALLBACK(select_stream), &data);

    ret = gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    data.loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(data.loop);

    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);
    return 0;
}

static gboolean select_stream(GstElement *src, guint num, GstCaps *caps, StreamData *data) {
    const gchar *media, *encoding;

    GstStructure *structure = gst_caps_get_structure(caps, 0);
    media = gst_structure_get_string(structure, "media");
    encoding = gst_structure_get_string(structure, "encoding-name");
    if (media && encoding && strcmp(media, "video") == 0 && strcmp(media, "H265") != 0) {
        return TRUE;
    }
    return FALSE;
}

static void pad_added(GstElement *src, GstPad *src_pad, const StreamData *data) {
    const gchar *src_name = NULL;
    const gchar *src_type = NULL;
    const gchar *caps = NULL;
    const gchar *sink_name = NULL;
    GstCaps *src_caps = NULL;
    GstStructure *caps_struct = NULL;
    GstElement *sink;
    GstPadLinkReturn ret;

    src_name = GST_PAD_NAME(src_pad);
    g_print("Source pad '%s' added:\n", src_name, GST_ELEMENT_NAME(src));

    src_caps = gst_pad_get_current_caps(src_pad);
    caps_struct = gst_caps_get_structure(src_caps, 0);
    src_type = gst_structure_get_name(caps_struct);
    caps = gst_caps_to_string(src_caps);
    g_print("Source pad '%s' type '%s' has %d capabilities: %s\n",
            src_name,
            src_type,
            gst_caps_get_size(src_caps),
            caps);

    sink = data->depay;
    GstPad *sink_pad = gst_element_get_static_pad(sink, "sink");
    if (gst_pad_is_linked(sink_pad)) {
        goto exit;
    }

    ret = gst_pad_link(src_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
        g_print("Source pad '%s' type '%s' could not be linked.\n", src_name, src_type);
    } else {
        sink_name = GST_ELEMENT_NAME(sink);
        g_print(
            "Source pad '%s' type '%s' linked to '%s' pad '%s'.\n",
            src_name,
            src_type,
            sink_name,
            GST_PAD_NAME(sink_pad));
    }

exit:
    gst_caps_unref(src_caps);
    gst_object_unref(sink_pad);
}

static void error_cb(GstBus *bus, GstMessage *msg, const StreamData *data) {
    GError *error;
    gchar *message;

    gst_message_parse_error(msg, &error, &message);
    g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), error->message);
    g_printerr("Debug information: %s\n", message ? message : "none");
    g_clear_error(&error);
    g_free(message);

    g_main_loop_quit(data->loop);
}
