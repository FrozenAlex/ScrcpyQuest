package com.genymobile.scrcpy.quest;

import com.genymobile.scrcpy.FakeContext;

import android.annotation.TargetApi;
import android.content.pm.ApplicationInfo;
import android.os.SystemClock;

import java.io.IOException;
import java.util.concurrent.TimeUnit;

@TargetApi(34)
final class QuestCommands {
    static final String PACKAGE = "com.oculus.magicislandcastingservice";

    private QuestCommands() {
    }

    static void checkInstalled() throws IOException {
        try {
            ApplicationInfo app = FakeContext.get().getPackageManager().getApplicationInfo(PACKAGE, 0);
            if (!app.enabled || (app.flags & ApplicationInfo.FLAG_SYSTEM) == 0) {
                throw new IOException("Quest cinematic requires the enabled, Meta-signed system casting app");
            }
        } catch (android.content.pm.PackageManager.NameNotFoundException e) {
            throw new IOException("Meta casting APK is missing; install it using Meta Quest Developer Hub", e);
        }
    }

    static void command(String... command) throws IOException {
        Process process = new ProcessBuilder(command).redirectErrorStream(true).start();
        try {
            if (!process.waitFor(5, TimeUnit.SECONDS)) {
                process.destroy();
                throw new IOException("Meta casting command timed out");
            }
            if (process.exitValue() != 0) {
                throw new IOException("Meta casting command failed: " + process.exitValue());
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Meta casting command interrupted", e);
        } finally {
            process.getInputStream().close();
        }
    }

    static void start(boolean initialize) throws IOException {
        command("am", "start-foreground-service", "-n", PACKAGE + "/.CastingService", "-a", PACKAGE + ".START_SERVICE",
                "--ez", "use_openxr", Boolean.toString(initialize));
    }

    static void set(int[] fov) throws IOException {
        command("am", "broadcast", "-a", PACKAGE + ".SET_DEVICE_EYE_FOV", "-p", PACKAGE,
                "--ei", "inward", Integer.toString(fov[0]), "--ei", "outward", Integer.toString(fov[1]),
                "--ei", "up", Integer.toString(fov[2]), "--ei", "down", Integer.toString(fov[3]));
    }

    static boolean verify(QuestFov reader, int[] expected) throws IOException {
        // The APK's command is asynchronous. A successful broadcast is not proof of a FOV change.
        for (int i = 0; i < 5; ++i) {
            if (matches(reader.read(), expected)) {
                return true;
            }
            SystemClock.sleep(100);
        }
        return false;
    }

    static boolean matches(int[] actual, int[] expected) {
        for (int i = 0; i < 4; ++i) {
            if (expected[i] >= 0 && actual[i] != expected[i]) {
                return false;
            }
        }
        return true;
    }

    static void apply(QuestFov reader, int[] requested, int[] expected) throws IOException {
        start(false);
        set(requested);
        if (verify(reader, expected)) {
            return;
        }
        // Initializing an already initialized Meta app is unsafe. Only initialize
        // if its command did not take effect, then verify again before capturing.
        start(true);
        set(requested);
        if (!verify(reader, expected)) {
            throw new IOException("Installed Meta casting APK did not apply the requested FOV");
        }
    }
}
