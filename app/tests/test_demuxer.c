#include "common.h"

#include <assert.h>
#include <string.h>

#include "demuxer.h"

// Feed deterministic transport truncations without relying on socket timing.
static const uint8_t *input;
static size_t remaining;
static enum sc_demuxer_status result;

ssize_t
net_recv_all(sc_socket socket, void *buf, size_t len) {
    (void) socket;
    size_t count = len < remaining ? len : remaining;
    memcpy(buf, input, count);
    input += count;
    remaining -= count;
    return count;
}

static bool
sink_open(struct sc_packet_sink *sink, const AVCodecParameters *params,
          const struct sc_stream_session *session) {
    (void) sink;
    (void) params;
    assert(session->video.width == 1920);
    assert(session->video.height == 1080);
    return true;
}

static void
sink_close(struct sc_packet_sink *sink) {
    (void) sink;
}

static enum sc_sink_result
sink_push(struct sc_packet_sink *sink, const AVPacket *packet) {
    (void) sink;
    (void) packet;
    return SC_SINK_OK;
}

static void
on_ended(struct sc_demuxer *demuxer, enum sc_demuxer_status status,
         void *userdata) {
    (void) demuxer;
    (void) userdata;
    result = status;
}

static void
check(const uint8_t *data, size_t len, enum sc_demuxer_status expected) {
    input = data;
    remaining = len;
    static const struct sc_packet_sink_ops ops = {
        .open = sink_open,
        .close = sink_close,
        .push = sink_push,
    };
    struct sc_packet_sink sink = {.ops = &ops};
    static const struct sc_demuxer_callbacks cbs = {.on_ended = on_ended};
    struct sc_demuxer demuxer;
    sc_demuxer_init(&demuxer, "test", (sc_socket) 1, false, &cbs, NULL);
    sc_packet_source_add_sink(&demuxer.packet_source, &sink);
    assert(sc_demuxer_start(&demuxer));
    sc_demuxer_join(&demuxer);
    assert(result == expected);
}

int main(void) {
    const uint8_t data[] = {
        'h', '2', '6', '4',
        0x80, 0, 0, 0, 0, 0, 7, 0x80, 0, 0, 4, 0x38,
        0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 8,
        0, 0, 0, 1, 0x65, 0x88, 0x84, 0,
    };
    // Loss before/within codec metadata, session header, packet header/payload.
    const size_t lengths[] = {0, 2, 4, 8, 16, 17, 28, 31, sizeof(data)};
    for (size_t i = 0; i < ARRAY_LEN(lengths); ++i) {
        check(data, lengths[i], SC_DEMUXER_STATUS_EOS);
    }
    const uint8_t configuration_error[] = {0, 0, 0, 1};
    check(configuration_error, sizeof(configuration_error),
          SC_DEMUXER_STATUS_ERROR);
    uint8_t invalid[sizeof(data)];
    memcpy(invalid, data, sizeof(data));
    invalid[27] = 0; // Invalid zero-length packet, not a transport failure.
    check(invalid, sizeof(invalid), SC_DEMUXER_STATUS_ERROR);
    return 0;
}
