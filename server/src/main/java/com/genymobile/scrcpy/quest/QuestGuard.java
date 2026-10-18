package com.genymobile.scrcpy.quest;

import com.genymobile.scrcpy.CleanUp;
import com.genymobile.scrcpy.Server;
import com.genymobile.scrcpy.util.Ln;

import android.annotation.TargetApi;
import android.net.LocalServerSocket;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.Arrays;
import java.util.concurrent.TimeUnit;

/** Pipe lifetime guard: restored even if the main app_process is killed. */
@TargetApi(34)
final class QuestGuard implements AutoCloseable {
    private final Process process;
    private final BufferedReader output;

    QuestGuard(int[] snapshot) throws IOException {
        ProcessBuilder builder = new ProcessBuilder("app_process", "/", CleanUp.class.getName(), "--quest-cinematic",
                Integer.toString(snapshot[0]), Integer.toString(snapshot[1]), Integer.toString(snapshot[2]), Integer.toString(snapshot[3]),
                Integer.toString(android.os.Process.myPid()));
        builder.environment().put("CLASSPATH", Server.SERVER_PATH);
        builder.redirectErrorStream(true);
        process = builder.start();
        output = new BufferedReader(new InputStreamReader(process.getInputStream()));
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
        try {
            while (System.nanoTime() < deadline) {
                if (output.ready()) {
                    String line = output.readLine();
                    if ("QUEST_RESTORE_READY".equals(line)) {
                        return;
                    }
                    Ln.d("Quest cleanup: " + line);
                } else if (!process.isAlive()) {
                    break;
                } else {
                    Thread.sleep(20);
                }
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        process.getOutputStream().close();
        throw new IOException("Could not arm Quest FOV restoration (another cinematic session may be active)");
    }

    @Override
    public void close() throws IOException {
        process.getOutputStream().close();
        try {
            if (!process.waitFor(15, TimeUnit.SECONDS)) {
                throw new IOException("Quest FOV restoration is still running");
            }
            String line;
            while ((line = output.readLine()) != null) {
                Ln.i("Quest cleanup: " + line);
            }
            if (process.exitValue() != 0) {
                throw new IOException("Quest FOV restoration failed; see device log");
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("Interrupted while restoring Quest FOV", e);
        } finally {
            output.close();
        }
    }

    static void run(String[] args) throws IOException {
        int[] snapshot = new int[4];
        for (int i = 0; i < 4; ++i) {
            snapshot[i] = Integer.parseInt(args[i + 1]);
        }
        try (LocalServerSocket lock = new LocalServerSocket("scrcpy-quest-cinematic"); QuestFov reader = new QuestFov()) {
            reader.read(); // All restoration dependencies must work before the parent may change FOV.
            System.out.println("QUEST_RESTORE_READY");
            System.out.flush();
            try {
                while (System.in.read() != -1) {
                    // Wait for EOF, including a disconnected/killed parent.
                }
            } finally {
                QuestCommands.apply(reader, snapshot, snapshot);
                System.out.println("Restored Quest FOV " + Arrays.toString(snapshot));
            }
        } finally {
            new File(Server.SERVER_PATH + ".quest-" + Integer.parseInt(args[5]) + ".so").delete();
        }
    }
}
