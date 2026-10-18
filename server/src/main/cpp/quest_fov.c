// Read the current eye FOV through the headset's installed OpenXR runtime.
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_NO_PROTOTYPES
#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#define printf(...) __android_log_print(ANDROID_LOG_DEBUG, "scrcpy-quest", __VA_ARGS__)
#include <unistd.h>

// Private extension ABI observed in the installed casting client.
// These are integer degree overrides, not XrFovf's floating-point radians.
typedef struct {
    XrStructureType type;
    void *next;
    int32_t inward, outward, up, down;
} QuestEyeFov;
typedef XrResult (*GetEyeFov)(XrSession, QuestEyeFov *);
static PFN_xrGetInstanceProcAddr get_proc;
static int resolve(XrInstance instance, const char *name, void *out) {
    XrResult result = get_proc(instance, name, (PFN_xrVoidFunction *) out);
    printf("%s: %d\n", name, result);
    return XR_SUCCEEDED(result);
}

JNIEXPORT jintArray JNICALL Java_com_genymobile_scrcpy_quest_QuestFovNative_read(JNIEnv *env, jclass cls, jobject context) {
    (void) cls;
    jint values[4] = {0};
    int status = 1;
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext egl_context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    PFN_xrDestroyInstance destroy_instance = NULL;
    PFN_xrDestroySession destroy_session = NULL;
    void *loader = dlopen("/system_ext/lib64/libopenxr_loader.so", RTLD_NOW | RTLD_LOCAL);
    if (!loader) { printf("OpenXR loader: %s\n", dlerror()); goto done; }
    get_proc = (PFN_xrGetInstanceProcAddr) dlsym(loader, "xrGetInstanceProcAddr");
    if (!get_proc) { printf("Missing xrGetInstanceProcAddr\n"); goto done; }
    PFN_xrInitializeLoaderKHR initialize;
    if (!resolve(XR_NULL_HANDLE, "xrInitializeLoaderKHR", &initialize)) goto done;
    JavaVM *vm;
    if ((*env)->GetJavaVM(env, &vm) != JNI_OK) goto done;
    XrLoaderInitInfoAndroidKHR init = {
        .type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
        .applicationVM = vm, .applicationContext = context,
    };
    XrResult result = initialize((XrLoaderInitInfoBaseHeaderKHR *) &init);
    printf("Initialize loader: %d\n", result);
    if (XR_FAILED(result)) goto done;

    PFN_xrEnumerateInstanceExtensionProperties enumerate;
    if (!resolve(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", &enumerate)) goto done;
    uint32_t count = 0;
    result = enumerate(NULL, 0, &count, NULL);
    printf("Enumerate extensions: %d, count=%u\n", result, count);
    if (XR_FAILED(result) || count > 4096) goto done;
    XrExtensionProperties *extensions = calloc(count, sizeof(*extensions));
    if (!extensions) goto done;
    for (uint32_t i = 0; i < count; ++i) extensions[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    result = enumerate(NULL, count, &count, extensions);
    int has_fov = 0;
    if (XR_SUCCEEDED(result)) {
        for (uint32_t i = 0; i < count; ++i) {
            const char *name = extensions[i].extensionName;
            if (strstr(name, "fov") || strstr(name, "headless") || strstr(name, "opengl")) printf("Extension: %s\n", name);
            if (!strcmp(name, "XR_METAI_headset_fov")) has_fov = 1;
        }
    }
    free(extensions);
    printf("XR_METAI_headset_fov advertised: %s\n", has_fov ? "yes" : "no");

    PFN_xrCreateInstance create_instance;
    if (!resolve(XR_NULL_HANDLE, "xrCreateInstance", &create_instance)) goto done;
    const char *enabled[] = {"XR_KHR_opengl_es_enable", "XR_METAI_headset_fov"};
    XrInstanceCreateInfo ci = {.type = XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy(ci.applicationInfo.applicationName, "scrcpy-quest");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 59);
    ci.enabledExtensionCount = 2;
    ci.enabledExtensionNames = enabled;
    result = create_instance(&ci, &instance);
    printf("Create instance with cinematic extension: %d\n", result);
    if (XR_FAILED(result)) goto done;
    if (!resolve(instance, "xrDestroyInstance", &destroy_instance)) goto done;
    PFN_xrGetSystem get_system;
    PFN_xrCreateSession create_session;
    PFN_xrGetOpenGLESGraphicsRequirementsKHR requirements;
    GetEyeFov get_fov;
    if (!resolve(instance, "xrGetSystem", &get_system)
            || !resolve(instance, "xrCreateSession", &create_session)
            || !resolve(instance, "xrDestroySession", &destroy_session)
            || !resolve(instance, "xrGetOpenGLESGraphicsRequirementsKHR", &requirements)
            || !resolve(instance, "xrGetHeadsetFovEyeMETAI", &get_fov)) goto done;
    XrSystemGetInfo gi = {.type = XR_TYPE_SYSTEM_GET_INFO, .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
    XrSystemId system;
    result = get_system(instance, &gi, &system);
    printf("Get system: %d\n", result);
    if (XR_FAILED(result)) goto done;
    XrGraphicsRequirementsOpenGLESKHR gr = {.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    result = requirements(instance, system, &gr);
    printf("Graphics requirements: %d\n", result);
    if (XR_FAILED(result)) goto done;
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) { printf("EGL initialization failed: %x\n", eglGetError()); goto done; }
    const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint configs;
    if (!eglChooseConfig(display, attrs, &config, 1, &configs) || !configs) goto done;
    const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    egl_context = eglCreateContext(display, config, EGL_NO_CONTEXT, ca);
    const EGLint pa[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    surface = eglCreatePbufferSurface(display, config, pa);
    if (egl_context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE
            || !eglMakeCurrent(display, surface, surface, egl_context)) { printf("EGL context failed: %x\n", eglGetError()); goto done; }
    XrGraphicsBindingOpenGLESAndroidKHR binding = {
        .type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
        .display = display, .config = config, .context = egl_context,
    };
    XrSessionCreateInfo si = {.type = XR_TYPE_SESSION_CREATE_INFO, .next = &binding, .systemId = system};
    result = create_session(instance, &si, &session);
    printf("Create unbegun session: %d\n", result);
    if (XR_FAILED(result)) goto done;
    QuestEyeFov fov = {.type = (XrStructureType) 1000594000};
    result = get_fov(session, &fov);
    printf("Read FOV: %d, inward=%d outward=%d up=%d down=%d\n", result, fov.inward, fov.outward, fov.up, fov.down);
    if (XR_SUCCEEDED(result)) {
        values[0] = fov.inward; values[1] = fov.outward;
        values[2] = fov.up; values[3] = fov.down;
        status = 0;
    }
done:
    if (session && destroy_session) printf("Destroy session: %d\n", destroy_session(session));
    if (instance && destroy_instance) printf("Destroy instance: %d\n", destroy_instance(instance));
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        if (egl_context != EGL_NO_CONTEXT) eglDestroyContext(display, egl_context);
        eglTerminate(display);
    }
    if (loader) dlclose(loader);
    if (status) {
        jclass exception = (*env)->FindClass(env, "java/io/IOException");
        (*env)->ThrowNew(env, exception, "Quest OpenXR FOV read failed; see scrcpy-quest in logcat");
        return NULL;
    }
    jintArray array = (*env)->NewIntArray(env, 4);
    if (array) (*env)->SetIntArrayRegion(env, array, 0, 4, values);
    return array;
}
