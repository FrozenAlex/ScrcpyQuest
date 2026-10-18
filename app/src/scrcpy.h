#ifndef SCRCPY_H
#define SCRCPY_H

#include "common.h"

#include "options.h"

enum scrcpy_exit_code {
    // Normal program termination
    SCRCPY_EXIT_SUCCESS,

    // No connection could be established
    SCRCPY_EXIT_FAILURE,

    // Device was disconnected while running
    SCRCPY_EXIT_DISCONNECTED,

    // Internal: mapped to FAILURE on exit, retried after a disconnection
    SCRCPY_EXIT_CONNECTION_FAILED,
};

struct sc_screen;

// A retained screen survives connection loss and is owned by the caller.
enum scrcpy_exit_code
scrcpy(struct scrcpy_options *options, char **connected_serial,
       struct sc_screen **retained_screen);

#endif
