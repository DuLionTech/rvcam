#include <gst/gst.h>

#define RTSP_RIGHT "rtsp://192.168.16.128:554/stream1"
#define RTSP_LEFT "rtsp://192.168.16.160:554/stream1"
#define RTSP_LATENCY 120

typedef struct {
    GstElement *rtsp;
    GstElement *depay;
    GstElement *parse;
    GstElement *decode;
    GstElement *convert;
    GstElement *flip;
} RtspData;

typedef struct {
    RtspData left;
    RtspData right;
    GstElement *compose;
    GstElement *sink;
    GstElement *pipeline;
    GMainLoop *loop;
} StreamData;

static gboolean select_stream_cb(GstElement *src, guint num, const GstCaps *caps, RtspData *data);

static void pad_added_cb(GstElement *src, GstPad *src_pad, const RtspData *data);

static GstElement *build_element(const gchar *factory_name, const gchar *prefix, const gchar *suffix);

static gboolean build_link(GstElement *src, GstElement *sink);

static gboolean build_channel(RtspData *data, GstElement *pipeline, GstPad *sink, const gchar *uri, const gchar *name);

static void error_cb(GstBus *bus, GstMessage *msg, const StreamData *data);

int main(int argc, char *argv[]) {
    StreamData data = {0};
    GstBus *bus;
    GstPad *sink_pad;

    gst_init(&argc, &argv);
    data.pipeline = gst_pipeline_new("rtsp-client");
    data.compose = gst_element_factory_make("compositor", "compositor");
    data.sink = gst_element_factory_make("autovideosink", "sink");
    if (!data.pipeline || !data.sink) {
        g_printerr("Not all element could be created.\n");
        return -1;
    }
    gst_bin_add_many(GST_BIN(data.pipeline), data.compose, data.sink, NULL);
    if (!gst_element_link(data.compose, data.sink)) {
        g_printerr("Could not link %s to %s", GST_ELEMENT_NAME(data.compose), GST_ELEMENT_NAME(data.sink));
        goto fail;
    }

    sink_pad = gst_element_request_pad_simple(data.compose, "sink_%u");
    g_object_set(G_OBJECT(sink_pad), "xpos", 0, "ypos", 0, "width", 1920, "height", 1080, NULL);
    if (!build_channel(&data.left, data.pipeline, sink_pad, RTSP_LEFT, "left_")) {
        gst_object_unref(sink_pad);
        goto fail;
    }
    gst_object_unref(sink_pad);

    sink_pad = gst_element_request_pad_simple(data.compose, "sink_%u");
    g_object_set(G_OBJECT(sink_pad), "xpos", 1920, "ypos", 0, "width", 1920, "height", 1080, NULL);
    if (!build_channel(&data.right, data.pipeline, sink_pad, RTSP_RIGHT, "right_")) {
        gst_object_unref(sink_pad);
        goto fail;
    }
    gst_object_unref(sink_pad);


    bus = gst_element_get_bus(data.pipeline);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(G_OBJECT(bus), "message::error", G_CALLBACK(error_cb), NULL);
    gst_object_unref(bus);

    if (gst_element_set_state(data.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        goto fail;
    }

    data.loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(data.loop);

    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);
    return 0;

fail:
    gst_object_unref(data.pipeline);
    return -1;
}

static gboolean build_channel(
    RtspData *data,
    GstElement *pipeline,
    GstPad *sink,
    const gchar *uri,
    const gchar *name) {
    g_autoptr(GstPad) src_pad = NULL;

    data->rtsp = build_element("rtspsrc", name, "rtsp");
    data->depay = build_element("rtph265depay", name, "depay");
    data->parse = build_element("h265parse", name, "parse");
    // data->decode = build_element("nvh265dec", name, "decode");
    data->decode = build_element("v4l2slh265dec", name, "decode");
    data->convert = build_element("videoconvert", name, "convert");
    data->flip = build_element("videoflip", name, "flip");
    if (!data->rtsp || !data->depay || !data->parse || !data->decode || !data->convert || !data->flip) {
        g_printerr("Not all rtsp element could be created.\n");
        return FALSE;
    }

    gst_bin_add_many(
        GST_BIN(pipeline),
        data->rtsp,
        data->depay,
        data->parse,
        data->decode,
        data->convert,
        data->flip,
        NULL);

    if (!build_link(data->depay, data->parse) ||
        !build_link(data->parse, data->decode) ||
        !build_link(data->decode, data->convert) ||
        !build_link(data->convert, data->flip)) {
        g_printerr("Could not link elements for channel");
        return FALSE;
    }

    src_pad = gst_element_get_static_pad(data->flip, "src");
    if (GST_PAD_LINK_FAILED(gst_pad_link(src_pad, sink))) {
        g_print("Could not link flip element to compositor.\n");
        return FALSE;
    }

    g_object_set(data->flip, "method", 4, NULL);
    g_object_set(data->rtsp, "location", uri, "protocols", 1, "latency", RTSP_LATENCY, NULL);
    g_signal_connect(data->rtsp, "pad-added", G_CALLBACK(pad_added_cb), data);
    g_signal_connect(data->rtsp, "select-stream", G_CALLBACK(select_stream_cb), data);
    return TRUE;
}

static GstElement *build_element(const gchar *factory_name, const gchar *prefix, const gchar *suffix) {
    g_autofree const gchar *element_name = g_strconcat(prefix, suffix, NULL);
    GstElement *element = gst_element_factory_make(factory_name, element_name);
    if (!element) {
        g_printerr("Factory %s could not create element %s.\n", factory_name, element_name);
    }
    return element;
}

static gboolean build_link(GstElement *src, GstElement *sink) {
    if (!gst_element_link(src, sink)) {
        g_printerr("Could not link %s to %s", GST_ELEMENT_NAME(src), GST_ELEMENT_NAME(sink));
        return FALSE;
    }
    return TRUE;
}

static gboolean select_stream_cb(GstElement *src, guint num, const GstCaps *caps, RtspData *data) {
    const gchar *media, *encoding;

     GstStructure *structure = gst_caps_get_structure(caps, 0);
    media = gst_structure_get_string(structure, "media");
    encoding = gst_structure_get_string(structure, "encoding-name");
    if (media && encoding && strcmp(media, "video") == 0 && strcmp(media, "H265") != 0) {
        return TRUE;
    }
    return FALSE;
}

static void pad_added_cb(GstElement *src, GstPad *src_pad, const RtspData *data) {
    g_autoptr(GstCaps) src_caps = NULL;
    g_autoptr(GstPad) sink_pad = NULL;
    GstStructure *caps_struct = NULL;
    const gchar *src_name = NULL;
    const gchar *src_type = NULL;
    const gchar *caps = NULL;

    src_name = GST_PAD_NAME(src_pad);
    g_print("Source pad '%s' added\n", src_name);

    src_caps = gst_pad_get_current_caps(src_pad);
    caps_struct = gst_caps_get_structure(src_caps, 0);
    src_type = gst_structure_get_name(caps_struct);
    caps = gst_caps_to_string(src_caps);
    g_print("Source pad '%s' type '%s' has %d capabilities: %s\n",
            src_name,
            src_type,
            gst_caps_get_size(src_caps),
            caps);

    sink_pad = gst_element_get_static_pad(data->depay, "sink");
    if (gst_pad_is_linked(sink_pad)) {
        g_printerr(
            "Source pad '%s' linked to '%s' pad '%s'.\n",
            src_name,
            GST_ELEMENT_NAME(data->depay),
            GST_PAD_NAME(sink_pad));
        return;
    }

    if (GST_PAD_LINK_FAILED(gst_pad_link(src_pad, sink_pad))) {
        g_print("Source pad '%s' type '%s' could not be linked.\n", src_name, src_type);
    } else {
        g_print(
            "Source pad '%s' type '%s' linked to '%s' pad '%s'.\n",
            src_name,
            src_type,
            GST_ELEMENT_NAME(data->depay),
            GST_PAD_NAME(sink_pad));
    }
}

static void error_cb(GstBus *bus, GstMessage *msg, const StreamData *data) {
    g_autofree GError *error;
    g_autofree gchar *message;

    gst_message_parse_error(msg, &error, &message);
    g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), error->message);
    g_printerr("Debug information: %s\n", message ? message : "none");
    g_main_loop_quit(data->loop);
}
