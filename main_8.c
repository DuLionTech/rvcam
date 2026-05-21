#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <string.h>

#define CHUNK_SIZE 1024
#define SAMPLE_RATE 44100

typedef struct {
    GstElement *app_source, *tee;
    GstElement *audio_queue, *audio_convert_1, *audio_resample, *audio_sink;
    GstElement *video_queue, *audio_convert_2, *visual, *video_convert, *video_sink;
    GstElement *app_queue, *app_sink;
    GstElement *pipeline;

    guint64 num_samples;
    gfloat a, b, c, d;
    guint source_id;
    GMainLoop *main_loop;
} StreamData;

static void start_feed(GstElement *source, guint size, StreamData *data);

static void stop_feed(GstElement *source, StreamData *data);

static gboolean push_data(StreamData *data);

static GstFlowReturn new_sample(GstElement *sink, StreamData *data);

static void error_cb(GstBus *bus, GstMessage *msg, const StreamData *data);

int main(int argc, char *argv[]) {
    StreamData data;
    GstPad *tee_audio_pad, *tee_video_pad, *tee_app_pad;
    GstPad *queue_audio_pad, *queue_video_pad, *queue_app_pad;
    GstAudioInfo info;
    GstCaps *audio_caps;
    GstBus *bus;

    memset(&data, 0, sizeof(data));
    data.b = 1;
    data.d = 1;

    gst_init(&argc, &argv);

    data.app_source = gst_element_factory_make("appsrc", "audio_source");
    data.tee = gst_element_factory_make("tee", "tee");

    data.audio_queue = gst_element_factory_make("queue", "audio_queue");
    data.audio_convert_1 = gst_element_factory_make("audioconvert", "audio_convert_1");
    data.audio_resample = gst_element_factory_make("audioresample", "audio_resample");
    data.audio_sink = gst_element_factory_make("autoaudiosink", "audio_sink");

    data.video_queue = gst_element_factory_make("queue", "video_queue");
    data.audio_convert_2 = gst_element_factory_make("audioconvert", "audio_convert_2");
    data.visual = gst_element_factory_make("wavescope", "visual");
    data.video_convert = gst_element_factory_make("videoconvert", "video_convert");
    data.video_sink = gst_element_factory_make("autovideosink", "video_sink");

    data.app_queue = gst_element_factory_make("queue", "app_queue");
    data.app_sink = gst_element_factory_make("appsink", "app_sink");

    data.pipeline = gst_element_factory_make("pipeline", "pipeline");

    if (!data.app_source || !data.tee) {
        g_printerr("Not all source elements could be created.\n");
        return -1;
    }
    if (!data.audio_queue || !data.audio_convert_1 || !data.audio_resample || !data.audio_sink) {
        g_printerr("Not all audio elements could be created.\n");
        return -1;
    }
    if (!data.video_queue || !data.audio_convert_2 || !data.visual || !data.video_convert || !data.video_sink) {
        g_printerr("Not all video elements could be created.\n");
        return -1;
    }
    if (!data.app_queue || !data.app_sink || !data.pipeline) {
        g_printerr("Not all app elements could be created.\n");
        return -1;
    }
    if (!data.pipeline) {
        g_printerr("Pipeline elements could be created.\n");
        return -1;
    }

    // Configure Wavescope
    g_object_set(data.visual, "shader", 0, "style", 0, NULL);

    // Configure appsrc
    gst_audio_info_set_format(&info, GST_AUDIO_FORMAT_S16, SAMPLE_RATE, 1, NULL);
    audio_caps = gst_audio_info_to_caps(&info);
    g_object_set(data.app_source, "caps", audio_caps, "format", GST_FORMAT_TIME, NULL);
    g_signal_connect(data.app_source, "need-data", G_CALLBACK(start_feed), &data);
    g_signal_connect(data.app_source, "enough-data", G_CALLBACK(stop_feed), &data);

    // Configure appsink
    g_object_set(data.app_sink, "emit-signals", TRUE, "caps", audio_caps, NULL);
    g_signal_connect(data.app_sink, "new-sample", G_CALLBACK(new_sample), &data);
    gst_caps_unref(audio_caps);

    gst_audio_info_set_format(&info, GST_AUDIO_FORMAT_S16, SAMPLE_RATE, 1, NULL);

    gst_bin_add_many(GST_BIN(data.pipeline), data.app_source, data.tee,
                     data.audio_queue, data.audio_convert_1, data.audio_resample, data.audio_sink,
                     data.video_queue, data.audio_convert_2, data.visual, data.video_convert, data.video_sink,
                     data.app_queue, data.app_sink, NULL);

    if (gst_element_link_many(data.app_source, data.tee, NULL) != TRUE) {
        g_printerr("Not all app source elements could be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    if (gst_element_link_many(
            data.audio_queue,
            data.audio_convert_1,
            data.audio_resample,
            data.audio_sink,
            NULL) != TRUE) {
        g_printerr("Not all audio sink elements could be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    if (gst_element_link_many(
            data.video_queue,
            data.audio_convert_2,
            data.visual,
            data.video_convert,
            data.video_sink,
            NULL) != TRUE) {
        g_printerr("Not all video sink elements could be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    if (gst_element_link_many(data.app_queue, data.app_sink, NULL) != TRUE) {
        g_printerr("Not all app sink elements could be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    tee_audio_pad = gst_element_request_pad_simple(data.tee, "src_%u");
    tee_video_pad = gst_element_request_pad_simple(data.tee, "src_%u");
    tee_app_pad = gst_element_request_pad_simple(data.tee, "src_%u");

    queue_audio_pad = gst_element_get_static_pad(data.audio_queue, "sink");
    queue_video_pad = gst_element_get_static_pad(data.video_queue, "sink");
    queue_app_pad = gst_element_get_static_pad(data.app_queue, "sink");

    if (gst_pad_link(tee_audio_pad, queue_audio_pad) != GST_PAD_LINK_OK) {
        g_printerr("Audio pipeline could not be linked to tee.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    if (gst_pad_link(tee_video_pad, queue_video_pad) != GST_PAD_LINK_OK) {
        g_printerr("Video pipeline could not be linked to tee.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    if (gst_pad_link(tee_app_pad, queue_app_pad) != GST_PAD_LINK_OK) {
        g_printerr("App pipeline could not be linked to tee.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    gst_object_unref(queue_audio_pad);
    gst_object_unref(queue_video_pad);
    gst_object_unref(queue_app_pad);

    bus = gst_element_get_bus(data.pipeline);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(G_OBJECT(bus), "message::error", G_CALLBACK(error_cb), &data);
    gst_object_unref(bus);

    // Start playing the pipeline
    gst_element_set_state(data.pipeline, GST_STATE_PLAYING);

    // Create a GLib Main Loop and set it to run
    data.main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(data.main_loop);

    gst_element_release_request_pad(data.tee, tee_audio_pad);
    gst_element_release_request_pad(data.tee, tee_video_pad);
    gst_element_release_request_pad(data.tee, tee_app_pad);
    gst_object_unref(tee_audio_pad);
    gst_object_unref(tee_video_pad);
    gst_object_unref(tee_app_pad);

    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);
    return 0;
}

static void start_feed(GstElement *source, guint size, StreamData *data) {
    if (data->source_id == 0) {
        g_print("Start feeding\n");
        data->source_id = g_idle_add(G_SOURCE_FUNC(push_data), data);
    }
}

static void stop_feed(GstElement *source, StreamData *data) {
    if (data->source_id != 0) {
        g_print("Stop feeding\n");
        g_source_remove(data->source_id);
        data->source_id = 0;
    }
}

static gboolean push_data(StreamData *data) {
    GstBuffer *buffer;
    GstFlowReturn ret;
    int i;
    GstMapInfo map;
    gint16 *raw;
    gint num_samples = CHUNK_SIZE >> 1;
    gfloat freq;

    buffer = gst_buffer_new_and_alloc(CHUNK_SIZE);
    GST_BUFFER_TIMESTAMP(buffer) = gst_util_uint64_scale(data->num_samples, GST_SECOND, SAMPLE_RATE);
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(num_samples, GST_SECOND, SAMPLE_RATE);

    // Generate some psychedelic waveforms
    gst_buffer_map(buffer, &map, GST_MAP_WRITE);
    raw = (gint16 *) map.data; // Ok, this is wierd
    data->c += data->d;
    data->d -= data->c / 1000;
    freq = 1100 + 1000 * data->d;
    for (i = 0; i < num_samples; i++) {
        data->a += data->b;
        data->b -= data->a / freq;
        raw[i] = (gint16) (500 * data->a);
    }
    gst_buffer_unmap(buffer, &map);
    data->num_samples += num_samples;

    g_signal_emit_by_name(data->app_source, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);
    if (ret != GST_FLOW_OK) {
        return FALSE;
    }

    return TRUE;
}

static GstFlowReturn new_sample(GstElement *sink, StreamData *data) {
    GstSample *sample;

    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
        g_print("*");
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    return GST_FLOW_FLUSHING;
}

static void error_cb(GstBus *bus, GstMessage *msg, const StreamData *data) {
    GError *error;
    gchar *info;

    gst_message_parse_error(msg, &error, &info);
    g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), error->message);
    g_printerr("Debug information: %s\n", info ? info : "none");
    g_clear_error(&error);
    g_free(info);

    g_main_loop_quit(data->main_loop);
}
