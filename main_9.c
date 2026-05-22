#include <string.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include "prefix.h"

typedef struct {
    GstDiscoverer *discoverer;
    GMainLoop *loop;
} StreamData;

static void on_discovered_cb(GstDiscoverer *discoverer, GstDiscovererInfo *info, GError *error, StreamData *data);

static void on_finished_cb(GstDiscoverer *discoverer, const StreamData *data);

static void print_topology(GstDiscovererStreamInfo *info, gint depth);

static void print_stream_info(GstDiscovererStreamInfo *info, gint depth);

static void print_tag_foreach(const GstTagList *tags, const gchar *tag, gpointer user_data);

int main(int argc, char *argv[]) {
    StreamData data;
    GError *error = NULL;
    gchar *uri = "rtsp://admin:Nitrox36@192.168.16.136:554/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif";

    if (argc > 1) {
        uri = argv[1];
    }

    // Instantiate the discoverer
    memset(&data, 0, sizeof(StreamData));
    gst_init(&argc, &argv);

    g_print("Discovering '%s'\n", uri);
    data.discoverer = gst_discoverer_new(60 * GST_SECOND, &error);
    if (!data.discoverer) {
        g_print("Error creating discoverer: %s\n", error->message);
        g_clear_error(&error);
        return -1;
    }

    // Connect to the interesting signals
    g_signal_connect(data.discoverer, "discovered", G_CALLBACK(on_discovered_cb), &data);
    g_signal_connect(data.discoverer, "finished", G_CALLBACK(on_finished_cb), &data);

    gst_discoverer_start(data.discoverer);
    if (!gst_discoverer_discover_uri_async(data.discoverer, uri)) {
        g_print("Failed to start discovering URI '%s'\n", uri);
        g_object_unref(data.discoverer);
        return -1;
    }

    data.loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(data.loop);

    gst_discoverer_stop(data.discoverer);
    g_object_unref(data.discoverer);
    g_main_loop_unref(data.loop);

    return 0;
}

static void on_discovered_cb(GstDiscoverer *discoverer, GstDiscovererInfo *info, GError *error, StreamData *data) {
    GstDiscovererResult result;
    const gchar *uri;
    const GstTagList *tags;
    GstDiscovererStreamInfo *stream_info;

    uri = gst_discoverer_info_get_uri(info);
    result = gst_discoverer_info_get_result(info);
    switch (result) {
        case GST_DISCOVERER_URI_INVALID:
            g_print("Invalid URI '%s'\n", uri);
            break;
        case GST_DISCOVERER_ERROR:
            g_print("Discoverer error: '%s'\n", error->message);
            break;
        case GST_DISCOVERER_TIMEOUT:
            g_print("Discoverer timeout\n");
            break;
        case GST_DISCOVERER_BUSY:
            g_print("Discoverer busy\n");
            break;
        case GST_DISCOVERER_MISSING_PLUGINS: {
            const GstStructure *gs;
            gchar *str;

            gs = gst_discoverer_info_get_misc(info);
            str = gst_structure_to_string(gs);
            g_print("Discoverer missing plugins: '%s'\n", str);
            g_free(str);
            break;
        }
        case GST_DISCOVERER_OK:
            break;
    }

    if (result != GST_DISCOVERER_OK) {
        g_print("Error discovering URI '%s'\n", uri);
        return;
    }

    g_print("\nDuration: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(gst_discoverer_info_get_duration(info)));

    tags = gst_discoverer_info_get_tags(info);
    if (tags) {
        g_print("Tags:\n");
        gst_tag_list_foreach(tags, print_tag_foreach, GINT_TO_POINTER(1));
    }
    g_print("Seekable: %s\n", gst_discoverer_info_get_seekable(info) ? "yes" : "no");
    g_print("\n");

    stream_info = gst_discoverer_info_get_stream_info(info);
    if (!stream_info) {
        return;
    }

    g_print("Stream information:\n");
    print_topology(stream_info, 1);
    gst_discoverer_info_unref(stream_info);
    g_print("\n");
}

static void on_finished_cb(GstDiscoverer *discoverer, const StreamData *data) {
    g_print("Discovery finished\n");
    g_main_loop_quit(data->loop);
}

static void print_topology(GstDiscovererStreamInfo *info, const gint depth) {
    GstDiscovererStreamInfo *next;
    if (!info) {
        return;
    }
    print_stream_info(info, depth);

    next = gst_discoverer_stream_info_get_next(info);
    if (next) {
        print_topology(next, depth + 1);
        gst_discoverer_stream_info_unref(next);
    } else if (GST_IS_DISCOVERER_CONTAINER_INFO(info)) {
        GList *tmp, *streams;

        streams = gst_discoverer_container_info_get_streams(GST_DISCOVERER_CONTAINER_INFO(info));
        for (tmp = streams; tmp; tmp = tmp->next) {
            GstDiscovererStreamInfo *tmp_info = GST_DISCOVERER_STREAM_INFO(tmp->data);
            print_topology(tmp_info, depth + 1);
        }
        gst_discoverer_stream_info_list_free(streams);
    }
}

static void print_stream_info(GstDiscovererStreamInfo *info, gint depth) {
    gchar *desc = NULL;
    GstCaps *caps;
    const GstTagList *tags;

    caps = gst_discoverer_stream_info_get_caps(info);
    if (caps) {
        if (gst_caps_is_fixed(caps)) {
            desc = gst_pb_utils_get_codec_description(caps);
        } else {
            desc = gst_caps_to_string(caps);
        }
        gst_caps_unref(caps);
    }
    g_print("%*s%s: %s\n", 2 * depth, " ", gst_discoverer_stream_info_get_stream_type_nick(info), desc ? desc : "");
    if (desc) {
        g_free(desc);
        desc = NULL;
    }

    tags = gst_discoverer_stream_info_get_tags(info);
    if (tags) {
        g_print("%*sTags:\n", 2 * (depth + 1), " ");
        gst_tag_list_foreach(tags, print_tag_foreach, GINT_TO_POINTER(depth + 2));
    }
}

static void print_tag_foreach(const GstTagList *tags, const gchar *tag, gpointer user_data) {
    GValue value = { 0,};
    gchar *str;
    gint depth = GPOINTER_TO_INT(user_data);

    gst_tag_list_copy_value(&value, tags, tag);
    if (G_VALUE_HOLDS_STRING(&value)) {
        str = g_value_dup_string(&value);
    } else {
        str = gst_value_serialize(&value);
    }
    g_print("%*s%s: %s\n", 2 * depth, " ", gst_tag_get_nick(tag), str);
    g_free(str);
    g_value_unset(&value);
}
