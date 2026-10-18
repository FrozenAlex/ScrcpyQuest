package com.genymobile.scrcpy.quest;

import com.genymobile.scrcpy.util.IO;

import android.annotation.TargetApi;
import android.os.SystemClock;

import java.io.IOException;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

@TargetApi(34)
final class QuestDeviceCommands implements QuestDeviceSettings.Backend {
    private static final String BOUNDARY_PROPERTY = "debug.oculus.guardian_pause";
    private static final Pattern PROXIMITY = Pattern.compile("(?m)^Virtual proximity state: (\\w+)\\s*$");

    private static String command(String... args) throws IOException {
        Process process = new ProcessBuilder(args).redirectErrorStream(true).start();
        FutureTask<String> output = new FutureTask<>(() -> IO.toString(process.getInputStream()));
        Thread reader = new Thread(output, "quest-command-output");
        reader.setDaemon(true);
        reader.start();
        try {
            if (!process.waitFor(5, TimeUnit.SECONDS)) {
                throw new IOException("Quest device command timed out: " + args[0]);
            }
            String result = output.get(1, TimeUnit.SECONDS).trim();
            if (process.exitValue() != 0) {
                throw new IOException("Quest device command failed: " + result);
            }
            return result;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Quest device command interrupted", e);
        } catch (java.util.concurrent.ExecutionException | java.util.concurrent.TimeoutException e) {
            throw new IOException("Cannot read Quest device command result", e);
        } finally {
            if (process.isAlive()) {
                process.destroyForcibly();
            }
            process.getInputStream().close();
        }
    }

    @Override
    public String getBoundaryPause() throws IOException {
        String value = command("getprop", BOUNDARY_PROPERTY);
        if (!value.isEmpty() && !"0".equals(value) && !"1".equals(value)) {
            throw new IOException("Unrecognized Quest boundary override: " + value);
        }
        return value;
    }

    @Override
    public void setBoundaryPause(String value) throws IOException {
        command("setprop", BOUNDARY_PROPERTY, value);
        if (!getBoundaryPause().equals(value)) {
            throw new IOException("Quest did not accept the boundary override");
        }
    }

    static String parseProximity(String dump) throws IOException {
        Matcher match = PROXIMITY.matcher(dump);
        if (!match.find()) {
            throw new IOException("Quest proximity state is unavailable on this firmware");
        }
        return match.group(1);
    }

    @Override
    public String getProximityOverride() throws IOException {
        return parseProximity(command("dumpsys", "vrpowermanager"));
    }

    private void verifyProximity(String expected) throws IOException {
        for (int attempt = 0; attempt < 5; ++attempt) {
            if (expected.equals(getProximityOverride())) {
                return;
            }
            SystemClock.sleep(100);
        }
        throw new IOException("Quest did not apply proximity override " + expected);
    }

    @Override
    public void holdProximityClosed() throws IOException {
        command("am", "broadcast", "--user", "0", "-a", "com.oculus.vrpowermanager.prox_close", "--ei", "duration", "30000");
        verifyProximity("CLOSE");
    }

    @Override
    public void releaseProximity() throws IOException {
        command("am", "broadcast", "--user", "0", "-a", "com.oculus.vrpowermanager.automation_disable");
        verifyProximity("DISABLED");
    }
}
