package com.genymobile.scrcpy.quest;

import java.io.IOException;

/** Entry point used by scrcpy's detached cleanup subprocess. */
public final class QuestCleanup {
    private QuestCleanup() {
    }

    public static void run(String[] args) throws IOException {
        QuestGuard.run(args);
    }
}
