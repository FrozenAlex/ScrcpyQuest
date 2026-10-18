package com.genymobile.scrcpy.quest;

import com.genymobile.scrcpy.FakeContext;
import com.genymobile.scrcpy.wrappers.ServiceManager;

import android.annotation.TargetApi;
import android.content.AttributionSource;
import android.content.ContentResolver;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.IContentProvider;
import android.os.Binder;
import android.os.Process;

import java.lang.reflect.Field;

/** Shell context which permits the OpenXR loader to load the runtime's Java classes. */
@TargetApi(34)
final class QuestContext extends ContextWrapper {
    private final ContentResolver resolver = new ContentResolver(this) {
        @SuppressWarnings({"unused", "ProtectedMemberInFinalClass"})
        protected IContentProvider acquireProvider(Context context, String name) {
            return ServiceManager.getActivityManager().getContentProviderExternal(name, new Binder());
        }

        @SuppressWarnings({"unused", "ProtectedMemberInFinalClass"})
        protected IContentProvider acquireUnstableProvider(Context context, String name) {
            return acquireProvider(context, name);
        }

        @SuppressWarnings("unused")
        public boolean releaseProvider(IContentProvider provider) {
            return false;
        }

        @SuppressWarnings("unused")
        public boolean releaseUnstableProvider(IContentProvider provider) {
            return false;
        }

        @SuppressWarnings("unused")
        public void unstableProviderDied(IContentProvider provider) {
            // Provider lifetime is bounded by this shell process.
        }
    };

    QuestContext() throws ReflectiveOperationException, android.content.pm.PackageManager.NameNotFoundException {
        super(FakeContext.get().getBaseContext().createPackageContext(FakeContext.PACKAGE_NAME, 0));
        // ContextImpl otherwise inherits the system context's "android" attribution.
        Field attribution = getBaseContext().getClass().getDeclaredField("mAttributionSource");
        attribution.setAccessible(true);
        attribution.set(getBaseContext(), getAttributionSource());
        Field opPackage = getBaseContext().getClass().getDeclaredField("mOpPackageName");
        opPackage.setAccessible(true);
        opPackage.set(getBaseContext(), FakeContext.PACKAGE_NAME);
    }

    @Override
    public String getPackageName() {
        return FakeContext.PACKAGE_NAME;
    }

    @Override
    public String getOpPackageName() {
        return FakeContext.PACKAGE_NAME;
    }

    @Override
    public AttributionSource getAttributionSource() {
        return new AttributionSource.Builder(Process.SHELL_UID).setPackageName(FakeContext.PACKAGE_NAME).build();
    }

    @Override
    public Context getApplicationContext() {
        return this;
    }

    @Override
    public ContentResolver getContentResolver() {
        return resolver;
    }
}
