package com.genymobile.scrcpy.quest;

import com.genymobile.scrcpy.BuildConfig;
import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.model.ConfigurationException;
import com.genymobile.scrcpy.model.Size;

import org.junit.Assert;
import org.junit.Test;

public class QuestCaptureTest {
    @Test
    public void sizesPreserveAspectRatioAndAlignment() {
        for (int alignment : new int[] {1, 2, 4, 8, 16}) {
            for (int maxSize : new int[] {320, 800, 1024, 1280, 1600, 1920, 2560}) {
                Size size = QuestCapture.computeSize(maxSize, alignment);
                Assert.assertEquals(size.getWidth() * 9, size.getHeight() * 16);
                Assert.assertEquals(0, size.getWidth() % alignment);
                Assert.assertEquals(0, size.getHeight() % alignment);
                Assert.assertTrue(size.getWidth() <= Math.min(1920, maxSize));
            }
        }
        Assert.assertEquals(new Size(1920, 1080), QuestCapture.computeSize(0, 8));
        Assert.assertEquals(new Size(1280, 720), QuestCapture.computeSize(1280, 8));
    }

    @Test(expected = IllegalArgumentException.class)
    public void rejectsSizeBelowAlignment() {
        QuestCapture.computeSize(15, 8);
    }

    @Test
    public void serverOptionIsOptIn() {
        Assert.assertFalse(Options.parse(new String[] {BuildConfig.VERSION_NAME}).getQuestCinematic());
        Options options = Options.parse(new String[] {BuildConfig.VERSION_NAME, "quest_cinematic=true", "max_fps=30"});
        Assert.assertTrue(options.getQuestCinematic());
        Assert.assertEquals(30, options.getMaxFps(), 0);
    }

    @Test
    public void fovVerificationAllowsOnlyRequestedDefaults() {
        int[] preset = {57, 57, -1, -1};
        Assert.assertTrue(QuestCommands.matches(new int[] {57, 57, 44, 55}, preset));
        Assert.assertFalse(QuestCommands.matches(new int[] {40, 54, 44, 55}, preset));
        Assert.assertFalse(QuestCommands.matches(new int[] {40, 54, 44, 54}, new int[] {40, 54, 44, 55}));
        Assert.assertTrue(QuestCommands.matches(new int[] {40, 54, 44, 55}, new int[] {40, 54, 44, 55}));
    }

    @Test(expected = ConfigurationException.class)
    public void rejectsDisabledVideoOnServer() throws Exception {
        QuestCapture.validate(Options.parse(new String[] {BuildConfig.VERSION_NAME, "quest_cinematic=true", "video=false"}));
    }

    @Test(expected = ConfigurationException.class)
    public void rejectsCameraOnServer() throws Exception {
        QuestCapture.validate(Options.parse(new String[] {BuildConfig.VERSION_NAME, "quest_cinematic=true", "video_source=camera"}));
    }
}
