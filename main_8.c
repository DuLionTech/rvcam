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

static gboolean push_data(StreamData *data);

static void start_feed(GstElement *source, guint size, StreamData *data);

static void stop_feed(GstElement *source, StreamData *data);

static GstFlowReturn new_sample(GstElement *sink, StreamData *data);

static void error_cb(GstBus *bus, GstMessage *msg, StreamData *data);

int main(int argc, char *argv[]) {
    StreamData data;
    return 0;
}

static gboolean push_data(StreamData *data) {
    return TRUE;
}

static void start_feed(GstElement *source, guint size, StreamData *data) {
}

static void stop_feed(GstElement *source, StreamData *data) {
}

static GstFlowReturn new_sample(GstElement *sink, StreamData *data) {
}

static void error_cb(GstBus *bus, GstMessage *msg, StreamData *data) {
}
