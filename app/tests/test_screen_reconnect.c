#include "common.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "screen.h"

static void
dispatch(struct sc_screen *screen) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        sc_screen_handle_event(screen, &event);
    }
}

int main(void) {
    assert(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
    assert(SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software"));
    assert(SDL_Init(SDL_INIT_VIDEO));
    struct sc_screen screen;
    struct sc_screen_params params = {
        .video = true,
        .reconnect = true,
        .window_title = "Reconnect test",
        .window_x = 10,
        .window_y = 20,
        .window_width = 640,
        .window_height = 360,
        .hwdec_mode = SC_HWDEC_MODE_DISABLED,
        .render_fit = SC_RENDER_FIT_LETTERBOX,
    };
    assert(sc_screen_init(&screen, &params));
    SDL_Window *window = screen.window;
    SDL_Renderer *renderer = screen.renderer;
    SDL_WindowID id = SDL_GetWindowID(window);
    AVCodecContext *ctx = avcodec_alloc_context3(NULL);
    assert(ctx);
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->width = 160;
    ctx->height = 90;
    struct sc_stream_session session = {.video = {.width = 160, .height = 90}};
    AVFrame *frame = av_frame_alloc();
    assert(frame);
    frame->format = ctx->pix_fmt;
    frame->width = ctx->width;
    frame->height = ctx->height;
    assert(av_frame_get_buffer(frame, 0) == 0);

    int x, y, width, height;
    for (unsigned i = 0; i < 3; ++i) {
        assert(screen.frame_sink.ops->open(&screen.frame_sink, ctx, &session));
        dispatch(&screen);
        assert(av_frame_make_writable(frame) == 0);
        frame->data[0][0] = 20 + i;
        assert(screen.frame_sink.ops->push(&screen.frame_sink, frame) == SC_SINK_OK);
        dispatch(&screen);
        assert(screen.frame->data[0][0] == 20 + i);
        if (!i) {
            // Simulate user movement/resizing after the initial CLI geometry.
            assert(SDL_SetWindowPosition(window, 125, 175));
            assert(SDL_SetWindowSize(window, 800, 450));
            dispatch(&screen);
            assert(SDL_GetWindowPosition(window, &x, &y));
            assert(SDL_GetWindowSize(window, &width, &height));
        } else {
            int new_x, new_y, new_width, new_height;
            assert(SDL_GetWindowPosition(window, &new_x, &new_y));
            assert(SDL_GetWindowSize(window, &new_width, &new_height));
            assert(x == new_x && y == new_y);
            assert(width == new_width && height == new_height);
        }
        assert(screen.window == window && screen.renderer == renderer);
        assert(SDL_GetWindowID(window) == id);
        assert(!(SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN));

        sc_screen_set_paused(&screen, true);
        assert(screen.frame_sink.ops->push(&screen.frame_sink, frame) == SC_SINK_OK);
        dispatch(&screen);
        sc_screen_set_paused(&screen, false);
        assert(screen.frame->data[0][0] == 20 + i);

        // Leave a frame and an OPEN_WINDOW payload queued at disconnect.
        assert(screen.frame_sink.ops->push(&screen.frame_sink, frame) == SC_SINK_OK);
        struct sc_size *size = malloc(sizeof(*size));
        assert(size);
        *size = (struct sc_size) {160, 90};
        assert(sc_push_event_with_data(SC_EVENT_OPEN_WINDOW, size));
        screen.frame_sink.ops->close(&screen.frame_sink);
        SDL_Event loss = {.type = SC_EVENT_DEVICE_DISCONNECTED};
        sc_screen_handle_event(&screen, &loss);
        sc_screen_suspend(&screen);
        assert(screen.disconnected && !screen.disconnect_started);
        assert(!sc_frame_buffer_has_frame(&screen.fb));
        assert(!SDL_HasEvent(SC_EVENT_OPEN_WINDOW));
        assert(!SDL_HasEvent(SC_EVENT_NEW_FRAME));
        assert(screen.frame->data[0][0] == 20 + i);
        assert(sc_texture_get(&screen.tex));

        // Offline input must not touch old session input objects.
        screen.im.kp = (struct sc_key_processor *) (uintptr_t) 1;
        SDL_Event key = {.key = {.type = SDL_EVENT_KEY_DOWN, .key = SDLK_A}};
        sc_screen_handle_event(&screen, &key);
        dispatch(&screen);
        sc_screen_reconnect(&screen, &params);
        assert(!screen.disconnected && !screen.im.kp);
        assert(!strcmp(SDL_GetWindowTitle(window), params.window_title));
    }
    sc_screen_interrupt(&screen);
    sc_screen_join(&screen);
    sc_screen_destroy(&screen);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    SDL_Quit();
    return 0;
}
