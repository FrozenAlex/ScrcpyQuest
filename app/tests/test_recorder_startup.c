#include "common.h"

#include <assert.h>
#include <stdio.h>

#include "recorder.h"

struct result {
    bool ended;
    enum sc_recorder_status status;
};

static void
on_ended(struct sc_recorder *recorder, enum sc_recorder_status status,
         void *userdata) {
    (void) recorder;
    struct result *result = userdata;
    result->status = status;
    result->ended = true;
}

int main(void) {
    static const struct sc_recorder_callbacks cbs = {.on_ended = on_ended};
    const char *filename = "test-recorder-startup.mkv";
    struct sc_recorder recorder;
    struct result result = {0};
    assert(sc_recorder_init(&recorder, filename, SC_RECORD_FORMAT_MKV, true,
                            true, SC_ORIENTATION_0, &cbs, &result));
    assert(sc_recorder_start(&recorder));

    // A producer is allowed to open immediately, before the recording worker
    // has been scheduled. Its output context must already be valid.
    assert(recorder.ctx && recorder.ctx->pb);
    AVCodecParameters *params = avcodec_parameters_alloc();
    assert(params);
    params->codec_type = AVMEDIA_TYPE_VIDEO;
    params->codec_id = AV_CODEC_ID_H264;
    params->width = 1920;
    params->height = 1080;
    struct sc_stream_session session = {.video = {.width = 1920, .height = 1080}};
    assert(recorder.video_packet_sink.ops->open(&recorder.video_packet_sink,
                                               params, &session));
    sc_recorder_stop(&recorder);
    sc_recorder_join(&recorder);
    assert(result.ended);
    assert(result.status == SC_RECORDER_STATUS_STOPPED);

    // A late audio stream must not access the context freed by the worker.
    params->codec_type = AVMEDIA_TYPE_AUDIO;
    params->codec_id = AV_CODEC_ID_OPUS;
    assert(!recorder.audio_packet_sink.ops->open(&recorder.audio_packet_sink,
                                                params, NULL));
    sc_recorder_destroy(&recorder);
    assert(!remove(filename));

    // A malformed config packet is still fatal, even if EOS follows it.
    result.ended = false;
    assert(sc_recorder_init(&recorder, filename, SC_RECORD_FORMAT_MKV, true,
                            false, SC_ORIENTATION_0, &cbs, &result));
    assert(sc_recorder_start(&recorder));
    params->codec_type = AVMEDIA_TYPE_VIDEO;
    params->codec_id = AV_CODEC_ID_H264;
    assert(recorder.video_packet_sink.ops->open(&recorder.video_packet_sink,
                                               params, &session));
    AVPacket *packet = av_packet_alloc();
    assert(packet);
    assert(av_new_packet(packet, 1) == 0);
    packet->data[0] = 0;
    packet->pts = 1; // Config packets must have AV_NOPTS_VALUE.
    assert(recorder.video_packet_sink.ops->push(&recorder.video_packet_sink,
                                               packet) == SC_SINK_OK);
    recorder.video_packet_sink.ops->close(&recorder.video_packet_sink);
    sc_recorder_join(&recorder);
    assert(result.ended);
    assert(result.status == SC_RECORDER_STATUS_ERROR);
    av_packet_free(&packet);
    avcodec_parameters_free(&params);
    sc_recorder_destroy(&recorder);
    assert(!remove(filename));

    result.ended = false;
    // The parent path was just removed, so output creation must fail before
    // start() reports success or any packet sink may access the context.
    assert(sc_recorder_init(&recorder, "test-recorder-startup.mkv/no.mp4",
                            SC_RECORD_FORMAT_MP4, true, false,
                            SC_ORIENTATION_0, &cbs, &result));
    assert(!sc_recorder_start(&recorder));
    assert(!result.ended);
    sc_recorder_destroy(&recorder);
    return 0;
}
