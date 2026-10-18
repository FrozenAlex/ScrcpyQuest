package com.genymobile.scrcpy.quest;

import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.model.ConfigurationException;
import com.genymobile.scrcpy.model.Orientation;
import com.genymobile.scrcpy.model.Size;
import com.genymobile.scrcpy.util.Ln;
import com.genymobile.scrcpy.video.CaptureControl;
import com.genymobile.scrcpy.video.SurfaceCapture;
import com.genymobile.scrcpy.video.VideoConstraints;
import com.genymobile.scrcpy.video.VideoSource;

import android.os.Build;
import android.view.Surface;

import java.io.IOException;
import java.util.Arrays;
import java.util.UUID;

public final class QuestCapture extends SurfaceCapture {
    private final int fps;
    private final String session = "scrcpy-" + UUID.randomUUID();
    private QuestCaptureService service;
    private QuestGuard guard;
    private Size size;
    private volatile boolean closed;

    public QuestCapture(Options options) {
        fps = options.getMaxFps() > 0 ? Math.max(1, (int) Math.ceil(options.getMaxFps())) : 60;
    }

    public static void validate(Options options) throws ConfigurationException {
        if (!options.getVideo() || options.getVideoSource() != VideoSource.DISPLAY || options.getDisplayId() != 0
                || options.getNewDisplay() != null || options.getCrop() != null || options.getAngle() != 0 || options.getFlexDisplay()
                || options.getCaptureOrientation() != Orientation.Orient0 || options.getCaptureOrientationLock() != Orientation.Lock.Unlocked) {
            throw new ConfigurationException("Quest cinematic requires the default display without crop, rotation or virtual display");
        }
        if (Build.VERSION.SDK_INT < 34 || !Arrays.asList(Build.SUPPORTED_ABIS).contains("arm64-v8a")) {
            throw new ConfigurationException("Quest cinematic currently requires ARM64 Horizon OS based on Android 14 or newer");
        }
    }

    // Since 16 and 9 are coprime, the scale must be a multiple of the encoder alignment.
    public static Size computeSize(int maxSize, int alignment) {
        int limit = maxSize > 0 ? Math.min(1920, maxSize) : 1920;
        int scale = limit / 16 / alignment * alignment;
        if (scale == 0) {
            throw new IllegalArgumentException("Quest cinematic max-size is too small for encoder alignment " + alignment);
        }
        return new Size(16 * scale, 9 * scale);
    }

    @Override
    protected void init(VideoConstraints constraints) throws IOException {
        applyNewVideoConstraints(constraints);
        QuestCommands.checkInstalled();
        service = new QuestCaptureService();
        service.linkToDeath(() -> {
            closed = true;
            Ln.e("Quest capture service died");
            getCaptureControl().reset(CaptureControl.RESET_REASON_TERMINATED);
        });
        try (QuestFov reader = new QuestFov()) {
            int[] snapshot = reader.read();
            guard = new QuestGuard(snapshot);
            try {
                // Zero requests the firmware's default vertical FOV, which may differ from a prior override.
                int[] expected = {57, 57, -1, -1};
                QuestCommands.apply(reader, new int[] {57, 57, 0, 0}, expected);
                Ln.i("Quest cinematic FOV: " + Arrays.toString(reader.read()) + "; original: " + Arrays.toString(snapshot));
            } catch (IOException | RuntimeException e) {
                release();
                throw e;
            }
        }
    }

    @Override
    public void start(Surface surface) throws IOException {
        if (closed) {
            throw new IOException("Quest capture service is closed");
        }
        service.start(surface, session, fps);
    }

    @Override
    public void stop() {
        // SurfaceEncoder releases this session's surface immediately afterwards.
        // The runtime stops its capture on BAD_SURFACE (also on process death).
        // Do not call the global stop transaction: firmware v10 ignores session ownership.
    }

    @Override
    public void release() {
        stop();
        if (guard != null) {
            try {
                guard.close();
            } catch (IOException e) {
                Ln.e("Could not restore Quest FOV", e);
            } finally {
                guard = null;
            }
        }
    }

    @Override
    public Size getSize() {
        return size;
    }

    @Override
    public boolean isClosed() {
        return closed;
    }

    @Override
    public long getFrameTimeoutMs() {
        return 10000;
    }

    @Override
    protected boolean applyNewVideoConstraints(VideoConstraints constraints) {
        size = computeSize(constraints.getMaxSize(), constraints.getAlignment());
        while (constraints.getEncoderCapabilities() != null
                && !constraints.getEncoderCapabilities().isSizeSupported(size.getWidth(), size.getHeight())) {
            int width = size.getWidth() - 16 * constraints.getAlignment();
            if (width <= 0) {
                return false;
            }
            size = computeSize(width, constraints.getAlignment());
        }
        return true;
    }
}
