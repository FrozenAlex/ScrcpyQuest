#include "common.h"

#include <assert.h>

#include "util/timeout.h"

static void
on_timeout(struct sc_timeout *timeout, void *userdata) {
    (void) timeout;
    unsigned *count = userdata;
    ++*count;
}

int main(void) {
    static const struct sc_timeout_callbacks cbs = {.on_timeout = on_timeout};
    struct sc_timeout timeout;
    unsigned count = 0;
    assert(sc_timeout_init(&timeout));
    assert(sc_timeout_start(&timeout, sc_tick_now() + SC_TICK_FROM_SEC(60),
                            &cbs, &count));
    sc_timeout_stop(&timeout);
    sc_timeout_join(&timeout);
    assert(count == 0); // Cancellation must not look like the time limit.
    sc_timeout_destroy(&timeout);

    assert(sc_timeout_init(&timeout));
    assert(sc_timeout_start(&timeout, sc_tick_now() - 1, &cbs, &count));
    sc_timeout_join(&timeout);
    assert(count == 1);
    sc_timeout_destroy(&timeout);
    return 0;
}
