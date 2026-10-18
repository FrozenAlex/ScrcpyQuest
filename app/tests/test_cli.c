#include "common.h"

#include <assert.h>
#include <string.h>

#include "cli.h"
#include "options.h"

static void test_flag_version(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };

    char *argv[] = {"scrcpy", "-v"};

    bool ok = scrcpy_parse_args(&args, 2, argv);
    assert(ok);
    assert(!args.help);
    assert(args.version);
}

static void test_flag_help(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };

    char *argv[] = {"scrcpy", "-v"};

    bool ok = scrcpy_parse_args(&args, 2, argv);
    assert(ok);
    assert(!args.help);
    assert(args.version);
}

static void test_options(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };

    char *argv[] = {
        "scrcpy",
        "--always-on-top",
        "--video-bit-rate", "5M",
        "--crop", "100:200:300:400",
        "--fullscreen",
        "--max-fps", "30",
        "--max-size", "1024",
        // "--no-control" is not compatible with "--turn-screen-off"
        // "--no-playback" is not compatible with "--fulscreen"
        "--port", "1234:1236",
        "--push-target", "/sdcard/Movies",
        "--record", "file",
        "--record-format", "mkv",
        "--serial", "0123456789abcdef",
        "--show-touches",
        "--turn-screen-off",
        "--prefer-text",
        "--window-title", "my device",
        "--window-x", "100",
        "--window-y", "-1",
        "--window-width", "600",
        "--window-height", "0",
        "--window-borderless",
    };

    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv), argv);
    assert(ok);

    const struct scrcpy_options *opts = &args.opts;
    assert(opts->always_on_top);
    assert(opts->video_bit_rate == 5000000);
    assert(!strcmp(opts->crop, "100:200:300:400"));
    assert(opts->fullscreen);
    assert(!strcmp(opts->max_fps, "30"));
    assert(opts->max_size == 1024);
    assert(opts->port_range.first == 1234);
    assert(opts->port_range.last == 1236);
    assert(!strcmp(opts->push_target, "/sdcard/Movies"));
    assert(!strcmp(opts->record_filename, "file"));
    assert(opts->record_format == SC_RECORD_FORMAT_MKV);
    assert(!strcmp(opts->serial, "0123456789abcdef"));
    assert(opts->show_touches);
    assert(opts->turn_screen_off);
    assert(opts->key_inject_mode == SC_KEY_INJECT_MODE_TEXT);
    assert(!strcmp(opts->window_title, "my device"));
    assert(opts->window_x == 100);
    assert(opts->window_y == -1);
    assert(opts->window_width == 600);
    assert(opts->window_height == 0);
    assert(opts->window_borderless);
}

static void test_options2(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };

    char *argv[] = {
        "scrcpy",
        "--no-control",
        "--no-playback",
        "--record", "file.mp4", // cannot enable --no-playback without recording
    };

    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv), argv);
    assert(ok);

    const struct scrcpy_options *opts = &args.opts;
    assert(!opts->control);
    assert(!opts->video_playback);
    assert(!opts->audio_playback);
    assert(!strcmp(opts->record_filename, "file.mp4"));
    assert(opts->record_format == SC_RECORD_FORMAT_MP4);
}

static void test_parse_shortcut_mods(void) {
    uint8_t mods;
    bool ok;

    ok = sc_parse_shortcut_mods("lctrl", &mods);
    assert(ok);
    assert(mods == SC_SHORTCUT_MOD_LCTRL);

    ok = sc_parse_shortcut_mods("rctrl,lalt", &mods);
    assert(ok);
    assert(mods == (SC_SHORTCUT_MOD_RCTRL | SC_SHORTCUT_MOD_LALT));

    ok = sc_parse_shortcut_mods("lsuper,rsuper,lctrl", &mods);
    assert(ok);
    assert(mods == (SC_SHORTCUT_MOD_LSUPER
                  | SC_SHORTCUT_MOD_RSUPER
                  | SC_SHORTCUT_MOD_LCTRL));

    ok = sc_parse_shortcut_mods("", &mods);
    assert(!ok);

    ok = sc_parse_shortcut_mods("lctrl+", &mods);
    assert(!ok);

    ok = sc_parse_shortcut_mods("lctrl,", &mods);
    assert(!ok);
}

static void test_quest_cinematic(void) {
    struct scrcpy_cli_args args = {.opts = scrcpy_options_default};
    char *argv[] = {"scrcpy", "--quest-cinematic"};
    assert(scrcpy_parse_args(&args, ARRAY_LEN(argv), argv));
    assert(args.opts.quest_cinematic);
    assert(!strcmp(args.opts.max_fps, "60"));
    assert(args.opts.mouse_input_mode == SC_MOUSE_INPUT_MODE_DISABLED);
    assert(!scrcpy_options_default.quest_cinematic);

    args.opts = scrcpy_options_default;
    char *custom[] = {"scrcpy", "--max-fps=30", "--quest-cinematic",
                      "--max-size=1280", "--video-codec=h265", "--record=test.mkv"};
    assert(scrcpy_parse_args(&args, ARRAY_LEN(custom), custom));
    assert(!strcmp(args.opts.max_fps, "30"));
    assert(args.opts.max_size == 1280);
    assert(args.opts.video_codec == SC_CODEC_H265);

    char *conflicts[] = {"--video-source=camera", "--new-display=1920x1080",
                        "--display-id=1", "--crop=100:100:0:0",
                        "--capture-orientation=90", "--angle=10", "--no-video",
                        "--otg", "--flex-display"};
    for (size_t i = 0; i < ARRAY_LEN(conflicts); ++i) {
        args.opts = scrcpy_options_default;
        char *bad[] = {"scrcpy", "--quest-cinematic", conflicts[i]};
        assert(!scrcpy_parse_args(&args, ARRAY_LEN(bad), bad));
    }
}

static void test_quest_device_switches(void) {
    assert(!scrcpy_options_default.quest_disable_proximity);
    assert(!scrcpy_options_default.quest_disable_boundary);
    char *flags[] = {"--quest-disable-proximity", "--quest-disable-boundary"};
    for (size_t i = 0; i < ARRAY_LEN(flags); ++i) {
        struct scrcpy_cli_args args = {.opts = scrcpy_options_default};
        char *argv[] = {"scrcpy", flags[i]};
        assert(scrcpy_parse_args(&args, ARRAY_LEN(argv), argv));
        assert(args.opts.quest_disable_proximity == (i == 0));
        assert(args.opts.quest_disable_boundary == (i == 1));
        assert(!args.opts.quest_cinematic);
        assert(!args.opts.max_fps);

        args.opts = scrcpy_options_default;
        char *otg[] = {"scrcpy", flags[i], "--otg"};
        assert(!scrcpy_parse_args(&args, ARRAY_LEN(otg), otg));
    }
    struct scrcpy_cli_args args = {.opts = scrcpy_options_default};
    char *combined[] = {"scrcpy", "--quest-cinematic", flags[0], flags[1],
                        "--no-cleanup"};
    assert(scrcpy_parse_args(&args, ARRAY_LEN(combined), combined));
    assert(args.opts.quest_cinematic && args.opts.quest_disable_proximity
           && args.opts.quest_disable_boundary);
    assert(!strcmp(args.opts.max_fps, "60"));

    args.opts = scrcpy_options_default;
    char *audio[] = {"scrcpy", flags[0], flags[1], "--no-video"};
    assert(scrcpy_parse_args(&args, ARRAY_LEN(audio), audio));
    assert(!args.opts.video);
}

static void test_reconnect(void) {
    assert(!scrcpy_options_default.reconnect);
    struct scrcpy_cli_args args = {.opts = scrcpy_options_default};
    char *argv[] = {"scrcpy", "--reconnect", "--quest-cinematic",
                    "--quest-disable-proximity", "--quest-disable-boundary",
                    "--record=quest.mp4"};
    assert(scrcpy_parse_args(&args, ARRAY_LEN(argv), argv));
    assert(args.opts.reconnect);
    assert(args.opts.quest_cinematic);
    assert(!strcmp(args.opts.record_filename, "quest.mp4"));

    char *conflicts[] = {"--otg", "--list-encoders", "--list-displays",
                        "--list-apps", "--list-cameras"};
    for (size_t i = 0; i < ARRAY_LEN(conflicts); ++i) {
        args.opts = scrcpy_options_default;
        char *bad[] = {"scrcpy", "--reconnect", conflicts[i]};
        assert(!scrcpy_parse_args(&args, ARRAY_LEN(bad), bad));
    }
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_quest_cinematic();
    test_quest_device_switches();
    test_reconnect();
    test_flag_version();
    test_flag_help();
    test_options();
    test_options2();
    test_parse_shortcut_mods();
    return 0;
}
