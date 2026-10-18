#include "common.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#ifdef HAVE_V4L2
# include <libavdevice/avdevice.h>
#endif
#include <SDL3/SDL.h>

#include "adb/adb_device.h"
#include "cli.h"
#include "events.h"
#include "options.h"
#include "scrcpy.h"
#include "screen.h"
#ifdef HAVE_USB
# include "usb/scrcpy_otg.h"
#endif
#include "util/log.h"
#include "util/net.h"
#include "util/str.h"
#include "util/term.h"
#include "util/tick.h"
#include "version.h"

#ifdef _WIN32
#include <windows.h>
#endif

static bool
wait_reconnect(struct sc_screen *screen) {
    sc_tick deadline = sc_tick_now() + SC_TICK_FROM_SEC(2);
    for (;;) {
        sc_tick remaining = deadline - sc_tick_now();
        if (remaining <= 0) {
            return true;
        }
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, SC_TICK_TO_MS(remaining) + 1)) {
            if (event.type == SDL_EVENT_QUIT
                    || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                return false;
            }
            if (screen) {
                sc_screen_handle_event(screen, &event);
            }
        }
    }
}

static char *
next_recording(const char *original, unsigned *part) {
    while (*part < UINT_MAX) {
        char suffix[32];
        snprintf(suffix, sizeof(suffix), ".part%03u", ++*part);
        char *path = sc_str_insert_before_extension(original, suffix);
        if (!path) {
            LOG_OOM();
            return NULL;
        }
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(path, &info)) {
            return path;
        }
        // Never replace an existing segment, including one from another run.
        free(path);
    }
    LOGE("Recording segment counter exhausted");
    return NULL;
}

static enum scrcpy_exit_code
run_sessions(const struct scrcpy_options *options) {
    char *serial = NULL;
    struct sc_screen *screen = NULL;
    unsigned record_part = 1;
    bool retrying = false;
    enum scrcpy_exit_code ret;
    for (;;) {
        struct scrcpy_options attempt = *options;
        char *recording = NULL;
        if (retrying) {
            // Pin the actual transport selected during the first connection.
            // A different attached device must never be selected on retry.
            attempt.select_usb = false;
            attempt.select_tcpip = false;
            bool tcpip = sc_adb_device_get_type(serial)
                      == SC_ADB_DEVICE_TYPE_TCPIP;
            attempt.serial = tcpip ? NULL : serial;
            attempt.tcpip = tcpip;
            attempt.tcpip_dst = tcpip ? serial : NULL;
            if (options->record_filename) {
                recording = next_recording(options->record_filename,
                                             &record_part);
                if (!recording) {
                    ret = SCRCPY_EXIT_FAILURE;
                    break;
                }
                attempt.record_filename = recording;
            }
        }
        if (!sc_main_thread_init()) {
            free(recording);
            ret = SCRCPY_EXIT_FAILURE;
            break;
        }
#ifdef HAVE_USB
        ret = attempt.otg ? scrcpy_otg(&attempt)
                          : scrcpy(&attempt, options->reconnect ? &serial : NULL,
                                   &screen);
#else
        ret = scrcpy(&attempt, options->reconnect ? &serial : NULL, &screen);
#endif
        sc_main_thread_destroy();
        free(recording);

        // A quit event may have arrived during session teardown.
        if (SDL_HasEvent(SDL_EVENT_QUIT)
                || SDL_HasEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                || SDL_HasEvent(SC_EVENT_TIME_LIMIT_REACHED)) {
            ret = SCRCPY_EXIT_SUCCESS;
        } else if (ret == SCRCPY_EXIT_DISCONNECTED
                && (SDL_HasEvent(SC_EVENT_DEMUXER_ERROR)
                    || SDL_HasEvent(SC_EVENT_DECODER_ERROR)
                    || SDL_HasEvent(SC_EVENT_RECORDER_ERROR)
                    || SDL_HasEvent(SC_EVENT_CONTROLLER_ERROR)
                    || SDL_HasEvent(SC_EVENT_AOA_OPEN_ERROR))) {
            // A control-socket EOF may race a real stream/recorder failure.
            ret = SCRCPY_EXIT_FAILURE;
        }
        if (!options->reconnect || !serial
                || (ret != SCRCPY_EXIT_DISCONNECTED
                    && ret != SCRCPY_EXIT_CONNECTION_FAILED)) {
            break;
        }

        // Session threads have joined and screen suspension freed payloads.
        // Keep SDL and native window events alive; discard only session events.
        SDL_FlushEvents(SC_EVENT_NEW_FRAME, SC_EVENT_DISCONNECTED_TIMEOUT);
        LOGW("Reconnecting to %s in 2 seconds (Ctrl+C to stop)...", serial);
        if (!wait_reconnect(screen)) {
            ret = SCRCPY_EXIT_SUCCESS;
            break;
        }
        retrying = true;
    }
    free(serial);
    if (screen) {
        sc_screen_interrupt(screen);
        sc_screen_join(screen);
        sc_screen_destroy(screen);
        free(screen);
    }
    SDL_Quit();
    return ret == SCRCPY_EXIT_CONNECTION_FAILED ? SCRCPY_EXIT_FAILURE : ret;
}

static int
main_scrcpy(int argc, char *argv[]) {
#ifdef _WIN32
    // disable buffering, we want logs immediately
    // even line buffering (setvbuf() with mode _IOLBF) is not sufficient
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
#endif

    printf("scrcpy " SCRCPY_VERSION
           " <https://github.com/Genymobile/scrcpy>\n");

    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
        .pause_on_exit = SC_PAUSE_ON_EXIT_UNDEFINED,
    };

#ifndef NDEBUG
    args.opts.log_level = SC_LOG_LEVEL_DEBUG;
#endif

    enum scrcpy_exit_code ret;

    bool term_title_saved = false;

    if (!scrcpy_parse_args(&args, argc, argv)) {
        ret = SCRCPY_EXIT_FAILURE;
        goto end;
    }

    sc_set_log_level(args.opts.log_level);

    if (args.opts.update_terminal_title) {
        sc_term_save_title();
        sc_term_set_title("scrcpy");
        term_title_saved = true;
    }

    if (args.help) {
        scrcpy_print_usage(argv[0]);
        ret = SCRCPY_EXIT_SUCCESS;
        goto end;
    }

    if (args.version) {
        scrcpy_print_version();
        ret = SCRCPY_EXIT_SUCCESS;
        goto end;
    }

#ifdef HAVE_V4L2
    if (args.opts.v4l2_device) {
        avdevice_register_all();
    }
#endif

    if (!net_init()) {
        ret = SCRCPY_EXIT_FAILURE;
        goto end;
    }

    sc_log_configure();

    ret = run_sessions(&args.opts);

    net_cleanup();

end:
    if (args.pause_on_exit == SC_PAUSE_ON_EXIT_TRUE ||
            (args.pause_on_exit == SC_PAUSE_ON_EXIT_IF_ERROR &&
                ret != SCRCPY_EXIT_SUCCESS)) {
        printf("Press Enter to continue...\n");
        getchar();
    }

    if (term_title_saved) {
        sc_term_set_title(""); // fallback if restore is ignored
        sc_term_restore_title();
    }

    return ret;
}

int
main(int argc, char *argv[]) {
#ifndef _WIN32
    return main_scrcpy(argc, argv);
#else
    (void) argc;
    (void) argv;
    int wargc;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv) {
        LOG_OOM();
        return SCRCPY_EXIT_FAILURE;
    }

    char **argv_utf8 = malloc((wargc + 1) * sizeof(*argv_utf8));
    if (!argv_utf8) {
        LOG_OOM();
        LocalFree(wargv);
        return SCRCPY_EXIT_FAILURE;
    }

    argv_utf8[wargc] = NULL;

    for (int i = 0; i < wargc; ++i) {
        argv_utf8[i] = sc_str_from_wchars(wargv[i]);
        if (!argv_utf8[i]) {
            LOG_OOM();
            for (int j = 0; j < i; ++j) {
                free(argv_utf8[j]);
            }
            LocalFree(wargv);
            free(argv_utf8);
            return SCRCPY_EXIT_FAILURE;
        }
    }

    LocalFree(wargv);

    int ret = main_scrcpy(wargc, argv_utf8);

    for (int i = 0; i < wargc; ++i) {
        free(argv_utf8[i]);
    }
    free(argv_utf8);

    return ret;
#endif
}
