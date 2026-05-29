#include <gtk/gtk.h>
#include <gst/gst.h>

#define RTSP_RIGHT "rtsp://192.168.16.128:554/stream1"
#define RTSP_LEFT "rtsp://192.168.16.160:554/stream1"
#define RTSP_LATENCY 67
#define RTSP_DECODER "nvh265dec"
// #define RTSP_DECODER "v4l2slh265dec"

typedef struct {
    GstElement *rtsp;
    GstElement *depay;
    GstElement *parse;
    GstElement *decode;
    GstElement *convert;
} ChannelData;

typedef struct {
    GstElement *pipeline;
    ChannelData left;
    ChannelData right;
    GstElement *compose;
    GstElement *overlay;
    GstElement *sink;
    GtkWidget *sink_widget;
    GMainLoop *loop;
} StreamData;

static gboolean pipeline_new(StreamData *data);

static gboolean channel_new(ChannelData *data, GstElement *pipeline, GstPad *pad, const gchar *uri, const gchar *name);

static GstElement *element_new(const gchar *factory_name, const gchar *prefix, const gchar *suffix);

static gboolean element_link(GstElement *src, GstElement *sink);

static gboolean select_stream_cb(GstElement *src, guint num, const GstCaps *caps, ChannelData *data);

static void pad_added_cb(GstElement *src, GstPad *src_pad, const ChannelData *data);

static void message_error_cb(GstBus *bus, GstMessage *msg, const StreamData *data);

GST_DEBUG_CATEGORY_STATIC(rvcam);
#define GST_CAT_DEFAULT rvcam

int main(int argc, char *argv[]) {
    GtkWidget *window;
    StreamData data = {0};

    g_setenv("GDK_GL", "gles", TRUE);
    gtk_init(&argc, &argv);
    gst_init(&argc, &argv);
    GST_DEBUG_CATEGORY_INIT(rvcam, "rvcam", 0, "Log for RV Camera Application");

    data.pipeline = gst_pipeline_new("rtsp-client");
    if (!data.pipeline || !pipeline_new(&data)) {
        GST_ERROR("Could not create rtsp pipeline");
        goto fail;
    }

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_container_add(GTK_CONTAINER(window), data.sink_widget);
    gtk_window_fullscreen(GTK_WINDOW(window));
    gtk_widget_show_all(window);

    if (gst_element_set_state(data.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        GST_ERROR("Unable to set the pipeline to the playing state.");
        goto fail;
    }

    gtk_main();

    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);
    return 0;

fail:
    gst_object_unref(data.pipeline);
    return -1;
}

static gboolean pipeline_new(StreamData *data) {
    GstBus *bus;
    GstPad *sink_pad;
    GstElement *gl_sink;

    gl_sink = element_new("gtkglsink", "main_", "gtkglsink");
    if (!gl_sink) {
        data->sink = element_new("gtksink", "main_", "gtksink");
        if (!data->sink) {
            return FALSE;
        }
        g_object_get(data->sink, "widget", &data->sink_widget, NULL);
    } else {
        data->sink = element_new("glsinkbin", "main_", "glsinkbin");
        if (!data->sink) {
            return FALSE;
        }
        g_object_set(data->sink, "sink", gl_sink, NULL);
        g_object_get(gl_sink, "widget", &data->sink_widget, NULL);
    }

    data->compose = element_new("compositor", "main_", "compositor");
    data->overlay = element_new("clockoverlay", "main_", "date-time");
    if (!data->compose || !data->overlay || !data->sink) {
        return FALSE;
    }
    gst_bin_add_many(GST_BIN(data->pipeline), data->compose, data->overlay, data->sink, NULL);
    if (!element_link(data->compose, data->overlay) ||
        !element_link(data->overlay, data->sink)) {
        return FALSE;
    }

    sink_pad = gst_element_request_pad_simple(data->compose, "sink_%u");
    g_object_set(G_OBJECT(sink_pad), "xpos", 0, "ypos", 0, "width", 1920, "height", 1080, NULL);
    if (!channel_new(&data->left, data->pipeline, sink_pad, RTSP_LEFT, "left_")) {
        gst_object_unref(sink_pad);
        return FALSE;
    }
    gst_object_unref(sink_pad);

    sink_pad = gst_element_request_pad_simple(data->compose, "sink_%u");
    g_object_set(G_OBJECT(sink_pad), "xpos", 1920, "ypos", 0, "width", 1920, "height", 1080, NULL);
    if (!channel_new(&data->right, data->pipeline, sink_pad, RTSP_RIGHT, "right_")) {
        gst_object_unref(sink_pad);
        return FALSE;
    }
    gst_object_unref(sink_pad);

    g_object_set(
        G_OBJECT(data->overlay),
        "time-format", "%F %T",
        "font-desc", "Sans 6",
        "halignment", 1,
        "valignment", 5,
        "ypos", 0.0,
        NULL);

    bus = gst_element_get_bus(data->pipeline);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(G_OBJECT(bus), "message::error", G_CALLBACK(message_error_cb), NULL);
    gst_object_unref(bus);

    return TRUE;
}

static gboolean channel_new(
    ChannelData *data,
    GstElement *pipeline,
    GstPad *pad,
    const gchar *uri,
    const gchar *name) {
    g_autoptr(GstPad) src_pad = NULL;

    data->rtsp = element_new("rtspsrc", name, "rtsp");
    data->depay = element_new("rtph265depay", name, "depay");
    data->parse = element_new("h265parse", name, "parse");
    data->decode = element_new(RTSP_DECODER, name, "decode");
    data->convert = element_new("videoconvert", name, "convert");
    if (!data->rtsp || !data->depay || !data->parse || !data->decode || !data->convert) {
        GST_ERROR("Not all rtsp element could be created.");
        return FALSE;
    }

    gst_bin_add_many(
        GST_BIN(pipeline),
        data->rtsp,
        data->depay,
        data->parse,
        data->decode,
        data->convert,
        NULL);

    if (!element_link(data->depay, data->parse) ||
        !element_link(data->parse, data->decode) ||
        !element_link(data->decode, data->convert)) {
        GST_ERROR("Could not link elements for channel");
        return FALSE;
    }

    src_pad = gst_element_get_static_pad(data->convert, "src");
    if (GST_PAD_LINK_FAILED(gst_pad_link(src_pad, pad))) {
        GST_INFO("Could not link convert element to compositor.");
        return FALSE;
    }

    g_object_set(data->rtsp, "location", uri, "protocols", 1, "latency", RTSP_LATENCY, NULL);
    g_signal_connect(data->rtsp, "pad-added", G_CALLBACK(pad_added_cb), data);
    g_signal_connect(data->rtsp, "select-stream", G_CALLBACK(select_stream_cb), data);
    return TRUE;
}

static GstElement *element_new(const gchar *factory_name, const gchar *prefix, const gchar *suffix) {
    g_autofree const gchar *element_name = g_strconcat(prefix, suffix, NULL);
    GstElement *element = gst_element_factory_make(factory_name, element_name);
    if (!element) {
        GST_ERROR("Factory %s could not create element %s.", factory_name, element_name);
    }
    return element;
}

static gboolean element_link(GstElement *src, GstElement *sink) {
    if (!gst_element_link(src, sink)) {
        GST_ERROR("Could not link %s to %s", GST_ELEMENT_NAME(src), GST_ELEMENT_NAME(sink));
        return FALSE;
    }
    return TRUE;
}

static gboolean select_stream_cb(GstElement *src, guint num, const GstCaps *caps, ChannelData *data) {
    const gchar *media, *encoding;

    GstStructure *structure = gst_caps_get_structure(caps, 0);
    media = gst_structure_get_string(structure, "media");
    encoding = gst_structure_get_string(structure, "encoding-name");
    if (media && encoding && strcmp(media, "video") == 0 && strcmp(media, "H265") != 0) {
        return TRUE;
    }
    return FALSE;
}

static void pad_added_cb(GstElement *src, GstPad *src_pad, const ChannelData *data) {
    g_autoptr(GstCaps) src_caps = NULL;
    g_autoptr(GstPad) sink_pad = NULL;
    GstStructure *caps_struct = NULL;
    const gchar *src_name = NULL;
    const gchar *src_type = NULL;
    const gchar *caps = NULL;

    src_name = GST_PAD_NAME(src_pad);
    src_caps = gst_pad_get_current_caps(src_pad);
    caps_struct = gst_caps_get_structure(src_caps, 0);
    src_type = gst_structure_get_name(caps_struct);
    caps = gst_caps_to_string(src_caps);
    GST_INFO("RTSP pad '%s' type '%s' has %d capabilities: %s",
             src_name,
             src_type,
             gst_caps_get_size(src_caps),
             caps);

    sink_pad = gst_element_get_static_pad(data->depay, "sink");
    if (gst_pad_is_linked(sink_pad)) {
        GST_ERROR(
            "RTSP pad '%s' linked to '%s' pad '%s'.",
            src_name,
            GST_ELEMENT_NAME(data->depay),
            GST_PAD_NAME(sink_pad));
        return;
    }

    if (GST_PAD_LINK_FAILED(gst_pad_link(src_pad, sink_pad))) {
        GST_INFO("RTSP pad '%s' type '%s' could not be linked.", src_name, src_type);
    } else {
        GST_INFO(
            "RTSP pad '%s' type '%s' linked to '%s' pad '%s'.",
            src_name,
            src_type,
            GST_ELEMENT_NAME(data->depay),
            GST_PAD_NAME(sink_pad));
    }
}

static void message_error_cb(GstBus *bus, GstMessage *msg, const StreamData *data) {
    g_autofree GError *error;
    g_autofree gchar *message;

    gst_message_parse_error(msg, &error, &message);
    GST_ERROR("Error received from element %s: %s", GST_OBJECT_NAME(msg->src), error->message);
    GST_ERROR("Debug information: %s", message ? message : "none");
    g_main_loop_quit(data->loop);
}
