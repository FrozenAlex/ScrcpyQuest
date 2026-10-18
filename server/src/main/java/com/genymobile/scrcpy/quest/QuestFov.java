package com.genymobile.scrcpy.quest;

import com.genymobile.scrcpy.FakeContext;
import com.genymobile.scrcpy.Server;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Process;
import dalvik.system.PathClassLoader;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

public final class QuestFov implements AutoCloseable {
    private static Method nativeRead;
    private final File library;
    private final Method read;
    private final Context context;

    public QuestFov() throws IOException {
        library = new File(Server.SERVER_PATH + ".quest-" + Process.myPid() + ".so");
        try {
            context = new QuestContext();
            read = load(library);
        } catch (Exception | LinkageError e) {
            library.delete();
            throw new IOException("Cannot load Quest FOV reader", e);
        }
    }

    private static synchronized Method load(File library) throws Exception {
        if (nativeRead != null) {
            return nativeRead;
        }
        ApplicationInfo runtime = FakeContext.get().getPackageManager().getApplicationInfo("com.oculus.systemdriver", 0);
        try (ZipFile zip = new ZipFile(Server.SERVER_PATH)) {
            ZipEntry entry = zip.getEntry("lib/arm64-v8a/libscrcpy-quest.so");
            if (entry == null) {
                throw new IOException("This server was built without the ARM64 Quest FOV bridge");
            }
            try (InputStream in = zip.getInputStream(entry); FileOutputStream out = new FileOutputStream(library)) {
                byte[] buffer = new byte[8192];
                int count;
                while ((count = in.read(buffer)) != -1) {
                    out.write(buffer, 0, count);
                }
            }
        }
        String paths = library.getParent() + ":/system_ext/lib64:/system/lib64:" + runtime.sourceDir + "!/lib/arm64-v8a";
        ClassLoader loader = new PathClassLoader(Server.SERVER_PATH, paths, ClassLoader.getSystemClassLoader().getParent());
        Class<?> cls = loader.loadClass(QuestFovNative.class.getName());
        cls.getMethod("load", String.class).invoke(null, library.getAbsolutePath());
        nativeRead = cls.getMethod("read", Context.class);
        return nativeRead;
    }

    public int[] read() throws IOException {
        try {
            return (int[]) read.invoke(null, context);
        } catch (IllegalAccessException | InvocationTargetException e) {
            throw new IOException("Cannot read Quest FOV", e);
        }
    }

    @Override
    public void close() {
        library.delete();
    }
}
