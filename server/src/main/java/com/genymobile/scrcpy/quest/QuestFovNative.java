package com.genymobile.scrcpy.quest;

import android.content.Context;

import java.io.IOException;

/** Loaded in its own classloader so the installed runtime's native library path is visible. */
public final class QuestFovNative {
    private QuestFovNative() {
    }

    public static void load(String path) {
        System.load(path);
    }

    public static synchronized native int[] read(Context context) throws IOException;
}
