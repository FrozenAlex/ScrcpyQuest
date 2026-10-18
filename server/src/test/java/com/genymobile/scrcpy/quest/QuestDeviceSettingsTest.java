package com.genymobile.scrcpy.quest;

import com.genymobile.scrcpy.BuildConfig;
import com.genymobile.scrcpy.Options;

import org.junit.Assert;
import org.junit.Test;

import java.io.IOException;

public class QuestDeviceSettingsTest {
    private static final class Backend implements QuestDeviceSettings.Backend {
        private String boundary = "0";
        private String proximity = "DISABLED";
        private int boundaryWrites;
        private int holds;
        private int releases;
        private boolean failBoundary;
        private boolean failHold;
        private boolean failRelease;

        @Override
        public String getBoundaryPause() {
            return boundary;
        }

        @Override
        public void setBoundaryPause(String value) throws IOException {
            boundary = value;
            ++boundaryWrites;
            if (failBoundary) {
                throw new IOException("boundary failure after write");
            }
        }

        @Override
        public String getProximityOverride() {
            return proximity;
        }

        @Override
        public void holdProximityClosed() throws IOException {
            proximity = "CLOSE";
            ++holds;
            if (failHold) {
                throw new IOException("proximity failure after write");
            }
        }

        @Override
        public void releaseProximity() throws IOException {
            proximity = "DISABLED";
            ++releases;
            if (failRelease) {
                throw new IOException("release failure");
            }
        }
    }

    @Test
    public void optionsAreIndependentAndOptIn() {
        Options defaults = Options.parse(new String[] {BuildConfig.VERSION_NAME});
        Assert.assertFalse(defaults.getQuestDisableProximity());
        Assert.assertFalse(defaults.getQuestDisableBoundary());
        Assert.assertFalse(defaults.getQuestDeviceSettings());
        Options proximity = Options.parse(new String[] {BuildConfig.VERSION_NAME, "quest_disable_proximity=true"});
        Assert.assertTrue(proximity.getQuestDisableProximity());
        Assert.assertFalse(proximity.getQuestDisableBoundary());
        Assert.assertFalse(proximity.getQuestCinematic());
        Options boundary = Options.parse(new String[] {BuildConfig.VERSION_NAME, "quest_disable_boundary=true"});
        Assert.assertTrue(boundary.getQuestDisableBoundary());
        Assert.assertFalse(boundary.getQuestDisableProximity());
        Assert.assertTrue(boundary.getQuestDeviceSettings());
    }

    @Test
    public void restoresOwnedOverridesAndRenewsLease() throws Exception {
        Backend backend = new Backend();
        try (QuestDeviceSettings settings = new QuestDeviceSettings(backend, true, true)) {
            settings.apply();
            Assert.assertEquals("1", backend.boundary);
            Assert.assertEquals("CLOSE", backend.proximity);
            settings.refresh();
            Assert.assertEquals(2, backend.holds);
        }
        Assert.assertEquals("0", backend.boundary);
        Assert.assertEquals("DISABLED", backend.proximity);
        Assert.assertEquals(1, backend.releases);
    }

    @Test
    public void preservesExistingOverridesAndTheirLifetime() throws Exception {
        Backend backend = new Backend();
        backend.boundary = "1";
        backend.proximity = "CLOSE";
        try (QuestDeviceSettings settings = new QuestDeviceSettings(backend, true, true)) {
            settings.apply();
            settings.refresh();
        }
        Assert.assertEquals(0, backend.boundaryWrites);
        Assert.assertEquals(0, backend.holds);
        Assert.assertEquals(0, backend.releases);
        Assert.assertEquals("CLOSE", backend.proximity);
    }

    @Test
    public void takesOverOnlyAfterExistingLeaseExpires() throws Exception {
        Backend backend = new Backend();
        backend.proximity = "CLOSE";
        try (QuestDeviceSettings settings = new QuestDeviceSettings(backend, true, false)) {
            settings.apply();
            Assert.assertEquals(0, backend.holds);
            backend.proximity = "DISABLED";
            settings.refresh();
            Assert.assertEquals(1, backend.holds);
        }
        Assert.assertEquals(1, backend.releases);
        Assert.assertEquals(0, backend.boundaryWrites);
    }

    @Test
    public void restoresEmptyPropertyAndLeavesUnrequestedProximityAlone() throws Exception {
        Backend backend = new Backend();
        backend.boundary = "";
        backend.proximity = "FAR";
        try (QuestDeviceSettings settings = new QuestDeviceSettings(backend, false, true)) {
            settings.apply();
        }
        Assert.assertEquals("", backend.boundary);
        Assert.assertEquals("FAR", backend.proximity);
        Assert.assertEquals(0, backend.releases);
    }

    @Test
    public void restoresAfterPartialApplyFailure() throws Exception {
        Backend backend = new Backend();
        backend.failHold = true;
        try (QuestDeviceSettings settings = new QuestDeviceSettings(backend, true, true)) {
            settings.apply();
            Assert.fail("Expected injected failure");
        } catch (IOException expected) {
            Assert.assertEquals("proximity failure after write", expected.getMessage());
        }
        Assert.assertEquals("0", backend.boundary);
        Assert.assertEquals("DISABLED", backend.proximity);
    }

    @Test
    public void registersRestorationBeforeBoundaryWrite() throws Exception {
        Backend backend = new Backend();
        try (QuestDeviceSettings settings = new QuestDeviceSettings(backend, true, true)) {
            backend.failBoundary = true;
            try {
                settings.apply();
                Assert.fail("Expected injected failure");
            } catch (IOException expected) {
                backend.failBoundary = false;
            }
        }
        Assert.assertEquals("0", backend.boundary);
        Assert.assertEquals(2, backend.boundaryWrites);
        Assert.assertEquals(0, backend.holds);
    }

    @Test
    public void attemptsBothRestorationsWhenOneFails() throws Exception {
        Backend backend = new Backend();
        QuestDeviceSettings settings = new QuestDeviceSettings(backend, true, true);
        settings.apply();
        backend.failBoundary = true;
        backend.failRelease = true;
        try {
            settings.close();
            Assert.fail("Expected injected failures");
        } catch (IOException expected) {
            Assert.assertEquals(1, expected.getSuppressed().length);
        }
        Assert.assertEquals(1, backend.releases);
        backend.failBoundary = false;
        backend.failRelease = false;
        settings.close();
        Assert.assertEquals("0", backend.boundary);
        Assert.assertEquals("DISABLED", backend.proximity);
    }

    @Test(expected = IOException.class)
    public void rejectsConflictingOverrideBeforeAnyChanges() throws Exception {
        Backend backend = new Backend();
        backend.proximity = "FAR";
        try {
            new QuestDeviceSettings(backend, true, true);
        } finally {
            Assert.assertEquals(0, backend.boundaryWrites);
        }
    }

    @Test
    public void parsesFirmwareStatus() throws Exception {
        Assert.assertEquals("CLOSE", QuestDeviceCommands.parseProximity("Virtual proximity state: CLOSE\nOther state: DISABLED\n"));
        Assert.assertEquals("DISABLED", QuestDeviceCommands.parseProximity("header\nVirtual proximity state: DISABLED\r\n"));
    }

    @Test(expected = IOException.class)
    public void rejectsUnrecognizedFirmwareStatus() throws Exception {
        QuestDeviceCommands.parseProximity("Service not found");
    }
}
