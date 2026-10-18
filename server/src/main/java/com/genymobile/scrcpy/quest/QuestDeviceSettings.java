package com.genymobile.scrcpy.quest;

import java.io.IOException;

/** Owns only overrides introduced by this session; existing overrides are left alone. */
final class QuestDeviceSettings implements AutoCloseable {
    interface Backend {
        String getBoundaryPause() throws IOException;
        void setBoundaryPause(String value) throws IOException;
        String getProximityOverride() throws IOException;
        void holdProximityClosed() throws IOException;
        void releaseProximity() throws IOException;
    }

    private final Backend backend;
    private final String boundarySnapshot;
    private final boolean proximity;
    private boolean boundaryChanged;
    private boolean proximityChanged;

    QuestDeviceSettings(Backend backend, boolean proximity, boolean boundary) throws IOException {
        this.backend = backend;
        this.proximity = proximity;
        boundarySnapshot = boundary ? backend.getBoundaryPause() : null;
        if (proximity) {
            checkProximity(backend.getProximityOverride());
        }
    }

    static void checkProximity(String state) throws IOException {
        if (!"CLOSE".equals(state) && !"DISABLED".equals(state)) {
            throw new IOException("Unsupported or conflicting Quest proximity override: " + state);
        }
    }

    void apply() throws IOException {
        if (boundarySnapshot != null && !"1".equals(boundarySnapshot)) {
            boundaryChanged = true; // Register restoration before attempting the write.
            backend.setBoundaryPause("1");
        }
        refresh();
    }

    void refresh() throws IOException {
        if (proximity) {
            String state = backend.getProximityOverride();
            checkProximity(state);
            if (proximityChanged || "DISABLED".equals(state)) {
                proximityChanged = true;
                backend.holdProximityClosed();
            }
            // An existing CLOSE override keeps its own lifetime. Take over only if it expires.
        }
    }

    @Override
    public void close() throws IOException {
        IOException error = null;
        if (boundaryChanged) {
            try {
                backend.setBoundaryPause(boundarySnapshot);
                boundaryChanged = false;
            } catch (IOException e) {
                error = e;
            }
        }
        if (proximityChanged) {
            try {
                backend.releaseProximity();
                proximityChanged = false;
            } catch (IOException e) {
                if (error == null) {
                    error = e;
                } else {
                    error.addSuppressed(e);
                }
            }
        }
        if (error != null) {
            throw error;
        }
    }
}
