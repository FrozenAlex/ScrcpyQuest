package com.genymobile.scrcpy.quest;

import com.genymobile.scrcpy.CleanUp;
import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.Server;
import com.genymobile.scrcpy.util.Ln;

import android.annotation.TargetApi;
import android.annotation.SuppressLint;
import android.net.LocalServerSocket;
import android.os.Build;
import android.os.Looper;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Detached owner of session-scoped Quest device overrides. */
@TargetApi(34)
public final class QuestDeviceSession implements AutoCloseable {
    private final Process process;
    private final BufferedReader output;
    private volatile boolean closing;

    @SuppressLint("ObsoleteSdkInt") // This check also provides a useful error when invoked on older devices.
    private QuestDeviceSession(Options options) throws IOException {
        if (Build.VERSION.SDK_INT < 34 || !Build.MODEL.contains("Quest")) {
            throw new IOException("Quest device switches require a Quest headset running Android 14 or newer");
        }
        ProcessBuilder builder = new ProcessBuilder("app_process", "/", CleanUp.class.getName(), "--quest-device-settings",
                Boolean.toString(options.getQuestDisableProximity()), Boolean.toString(options.getQuestDisableBoundary()));
        builder.environment().put("CLASSPATH", Server.SERVER_PATH);
        builder.redirectErrorStream(true);
        process = builder.start();
        output = new BufferedReader(new InputStreamReader(process.getInputStream()));
        try {
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20);
            boolean ready = false;
            StringBuilder diagnostics = new StringBuilder();
            while (System.nanoTime() < deadline) {
                if (output.ready()) {
                    String line = output.readLine();
                    if (line == null) {
                        break;
                    }
                    if ("QUEST_SETTINGS_READY".equals(line)) {
                        ready = true;
                        break;
                    }
                    Ln.d("Quest device settings: " + line);
                    diagnostics.append(line).append('\n');
                } else if (!process.isAlive()) {
                    break;
                } else {
                    Thread.sleep(20);
                }
            }
            if (!ready) {
                throw new IOException("Could not apply Quest device settings: " + diagnostics);
            }
        } catch (IOException | InterruptedException e) {
            if (e instanceof InterruptedException) {
                Thread.currentThread().interrupt();
            }
            process.getOutputStream().close();
            output.close();
            throw new IOException("Quest device settings startup failed", e);
        }
        Thread monitor = new Thread(() -> {
            try {
                int status = process.waitFor();
                if (!closing) {
                    Ln.e("Quest device settings helper stopped unexpectedly (status " + status + ")");
                    Looper.getMainLooper().quitSafely();
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }, "quest-settings-monitor");
        monitor.setDaemon(true);
        monitor.start();
        Ln.i("Quest device overrides active: proximity=" + options.getQuestDisableProximity() + ", boundary=" + options.getQuestDisableBoundary());
    }

    public static QuestDeviceSession start(Options options) throws IOException {
        return options.getQuestDeviceSettings() ? new QuestDeviceSession(options) : null;
    }

    @Override
    public void close() throws IOException {
        closing = true;
        process.getOutputStream().close();
        try {
            if (!process.waitFor(15, TimeUnit.SECONDS)) {
                throw new IOException("Quest device setting restoration is still running");
            }
            String line;
            while ((line = output.readLine()) != null) {
                Ln.i("Quest device settings: " + line);
            }
            if (process.exitValue() != 0) {
                throw new IOException("Quest device setting restoration failed; see device log");
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrupted while restoring Quest device settings", e);
        } finally {
            output.close();
        }
    }

    public static void run(String[] args) throws IOException, InterruptedException {
        boolean proximity = Boolean.parseBoolean(args[1]);
        boolean boundary = Boolean.parseBoolean(args[2]);
        try (LocalServerSocket lock = new LocalServerSocket("scrcpy-quest-device-settings");
                QuestDeviceSettings settings = new QuestDeviceSettings(new QuestDeviceCommands(), proximity, boundary)) {
            CountDownLatch disconnected = new CountDownLatch(1);
            Thread input = new Thread(() -> {
                try {
                    while (System.in.read() != -1) {
                        // Parent lifetime pipe; EOF also arrives after SIGKILL or disconnect.
                    }
                } catch (IOException e) {
                    // Treat a broken pipe as disconnection.
                } finally {
                    disconnected.countDown();
                }
            }, "quest-settings-lifetime");
            input.setDaemon(true);
            input.start();
            settings.apply();
            System.out.println("QUEST_SETTINGS_READY");
            System.out.flush();
            while (!disconnected.await(10, TimeUnit.SECONDS)) {
                settings.refresh();
            }
        }
        System.out.println("Restored session-owned boundary/proximity overrides");
    }
}
