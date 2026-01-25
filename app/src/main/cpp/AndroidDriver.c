#include <stdio.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <jni.h>
#include <android/asset_manager.h>
#include <android_native_app_glue.h>

#include "AndroidRenderer.h"
#include "AndroidDriver.h"

static struct android_app *gapp;
static int OGLESStarted = 0;
static int android_width, android_height;
static int is_app_paused = 0;

//#define AWINDOW_FLAG_FULLSCREEN = 0x00000400;
//#define AWINDOW_FLAG_KEEP_SCREEN_ON = 0x00000080;
//#define AWINDOW_FLAG_TURN_SCREEN_ON = 0x00200000;

EGLNativeWindowType native_window;

static const EGLint config_attr_list[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_BUFFER_SIZE, 32,
        EGL_STENCIL_SIZE, 0,
        EGL_DEPTH_SIZE, 16,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
};

static EGLint window_attr_list[] = {
        EGL_NONE
};

static const EGLint context_attr_list[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
};

EGLDisplay egl_display;
EGLSurface egl_surface;
EGLContext egl_context;

static int LastInternalW, LastInternalH;

void SwapBuffers(void)
{
    if (is_app_paused) return;
    if (gapp == NULL || gapp->window == NULL || egl_display == EGL_NO_DISPLAY) {
        printf("Error: No app here!\n");
        return;
    }

    if (!eglSwapBuffers(egl_display, egl_surface)) {
        EGLint err = eglGetError();
        if (err == EGL_BAD_SURFACE || err == EGL_CONTEXT_LOST) {
            is_app_paused = 1; // Stop drawing if surface is lost
        }
        return;
    }

    int events;
    struct android_poll_source* source;
    while (ALooper_pollOnce(0, NULL, &events, (void**)&source) >= 0) {
        if (source != NULL) source->process(gapp, source);
    }

//    android_width = ANativeWindow_getWidth(native_window);
//    android_height = ANativeWindow_getHeight(native_window);

    int current_w = ANativeWindow_getWidth(gapp->window);
    int current_h = ANativeWindow_getHeight(gapp->window);

//    glViewport(0, 0, android_width, android_height);
//    if (LastInternalW != android_width || LastInternalH != android_height)
//    {
//        LastInternalW = android_width;
//        LastInternalH = android_height;
//        InternalResize(LastInternalW, LastInternalH);
//    }

    if (current_w != android_width || current_h != android_height)
    {
        android_width = current_w;
        android_height = current_h;
        glViewport(0, 0, android_width, android_height);
        // Ensure InternalResize exists in your AndroidRenderer.c
        InternalResize(android_width, android_height);
    }
}

void GetScreenDimensions(int *x, int *y)
{
    *x = android_width;
    *y = android_height;
}

void AndroidMakeFullscreen(void)
{
    // These flags are thread-safe and can be called from the background thread
    // AWINDOW_FLAG_FULLSCREEN: Hides the status bar (on phones)
    // AWINDOW_FLAG_KEEP_SCREEN_ON: Prevents the watch/phone from dimming during gameplay

    int AWINDOW_FLAG_FULLSCREEN = 0x00000400;
    int AWINDOW_FLAG_KEEP_SCREEN_ON = 0x00000080;
    int AWINDOW_FLAG_TURN_SCREEN_ON = 0x00200000;

    ANativeActivity_setWindowFlags(gapp->activity,
                                   AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_TURN_SCREEN_ON,
                                   0);
}

void SetupApplication(void)
{
    is_app_paused = 0;
    EGLint egl_major, egl_minor;
    EGLConfig config;
    EGLint num_config;

    //This MUST be called before doing any initialization.
    int events;
    while (!OGLESStarted)
    {
        struct android_poll_source *source;
        if (ALooper_pollOnce(0, 0, &events, (void**)&source) >= 0)
        {
            if (source != NULL) source->process(gapp, source);
        }
    }

    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (egl_display == EGL_NO_DISPLAY)
    {
        printf("Error: No display found!\n");
        exit(1);
    }

    if (!eglInitialize(egl_display, &egl_major, &egl_minor))
    {
        printf("Error: eglInitialise failed!\n");
        exit(1);
    }

    eglChooseConfig(egl_display, config_attr_list, &config, 1,
                    &num_config);
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT,
                                   context_attr_list);

    if (egl_context == EGL_NO_CONTEXT)
    {
        printf("Error: eglCreateContext failed: 0x%08X\n", eglGetError());
        exit(1);
    }

    if (native_window && !gapp->window)
    {
        printf("WARNING: App restarted without a window. Cannot progress.\n");
        exit(0);
    }

    native_window = gapp->window;
    if (!native_window)
    {
        printf("FAULT: Cannot get window\n");
        exit(1);
    }
    android_width = ANativeWindow_getWidth(native_window);
    android_height = ANativeWindow_getHeight(native_window);

    printf("Window Size: %dx%d\n", android_width, android_height);

    egl_surface = eglCreateWindowSurface(egl_display, config, gapp->window, window_attr_list);

    if (egl_surface == EGL_NO_SURFACE)
    {
        printf("Error: eglCreateWindowSurface failed: 0x%08X\n", eglGetError());
        exit(1);
    }

    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context))
    {
        printf("Error: eglMakeCurrent() failed: 0x%08X\n", eglGetError());
        exit(1);
    }

    SetupBatchInternal();
}

int button_x[8] = {-1};
int button_y[8] = {-1};
int motion_x[8] = {0};
int motion_y[8] = {0};
bool button_down[8] = {false};

int32_t handle_input(struct android_app *app, AInputEvent *event)
{
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION)
    {
        int action = AMotionEvent_getAction(event);
        int whichSource = action >> 8;
        action &= AMOTION_EVENT_ACTION_MASK;
        size_t pointerCount = AMotionEvent_getPointerCount(event);
        if (pointerCount > 8) pointerCount = 8;

        for (size_t i = 0; i < pointerCount; ++i)
        {
            int x, y, index;
            x = (int) AMotionEvent_getX(event, i);
            y = (int) AMotionEvent_getY(event, i);
            index = AMotionEvent_getPointerId(event, i);

            if (action == AMOTION_EVENT_ACTION_POINTER_DOWN || action == AMOTION_EVENT_ACTION_DOWN)
            {
                int id = index;
                if (action == AMOTION_EVENT_ACTION_POINTER_DOWN && id != whichSource) continue;
                button_x[id] = x;
                button_y[id] = y;
                motion_x[index] = x;
                motion_y[index] = y;
                button_down[id] = true;
                ANativeActivity_showSoftInput(gapp->activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED);
            }
            else if (action == AMOTION_EVENT_ACTION_POINTER_UP || action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL)
            {
                int id = index;
                if (action == AMOTION_EVENT_ACTION_POINTER_UP && id != whichSource) continue;
                button_x[id] = -1;
                button_y[id] = -1;
                button_down[id] = false;
            }
            else if (action == AMOTION_EVENT_ACTION_MOVE)
            {
                motion_x[index] = x;
                motion_y[index] = y;
            }
        }
        return 1;
    }
    //KEY INPUT IS SIMPLY BROKEN
    return 0;
}

void HandleInput(void)
{
    int events;
    struct android_poll_source *source;
    while (ALooper_pollOnce(0, 0, &events, (void**)&source) >= 0)
    {
        if (source != NULL) source->process(gapp, source);
    }
}

void handle_cmd(struct android_app *app, int32_t cmd)
{
    switch (cmd)
    {
        case APP_CMD_INIT_WINDOW:
            //When returning from a back button suspension, this isn't called.
            if (!OGLESStarted)
            {
                OGLESStarted = 1;
                printf("Got start event\n");
            }
            else
            {
                SetupApplication();
            }
            break;
        case APP_CMD_TERM_WINDOW:
            if (egl_display != EGL_NO_DISPLAY)
            {
                eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                if (egl_context != EGL_NO_CONTEXT)
                    eglDestroyContext(egl_display, egl_context);
                if (egl_surface != EGL_NO_SURFACE)
                    eglDestroySurface(egl_display, egl_surface);
                eglTerminate(egl_display);

                egl_display = EGL_NO_DISPLAY;
                egl_context = EGL_NO_CONTEXT;
                egl_surface = EGL_NO_SURFACE;
                is_app_paused = 1;
            }
            break;
        case APP_CMD_DESTROY:
            //This gets called initially after back.
            ANativeActivity_finish(gapp->activity);
            break;
        default:
            break;
    }
}

static int android_read(void *cookie, char *buf, int size)
{
    return AAsset_read((AAsset *) cookie, buf, size);
}

static int android_write(void *cookie, const char *buf, int size)
{
    return 0; // can't provide write access to the apk
}

static fpos_t android_seek(void *cookie, fpos_t offset, int whence)
{
    return AAsset_seek((AAsset *) cookie, offset, whence);
}

static int android_close(void *cookie)
{
    AAsset_close((AAsset *) cookie);
    return 0;
}

FILE *android_fopen(const char *path, const char *mode)
{
    if (mode[0] == 'w') return NULL;

    const char *fixed_path = path;
    if (path[0] == '/') {
        fixed_path = path + 1;
    }

    __android_log_print(ANDROID_LOG_VERBOSE, "Doom", "Opening asset: %s", fixed_path);

    AAsset *asset = AAssetManager_open(gapp->activity->assetManager, path, 0);
    if (!asset) return NULL;

    return funopen(asset, android_read, android_write, android_seek, android_close);
}

__attribute__((unused))
void android_main(struct android_app *app)
{
    void doomgeneric_Create(int argc, char **argv);
    char *argv[] = { "main", NULL };

    gapp = app;
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

    while (app->window == NULL && !app->destroyRequested) {
        int events;
        struct android_poll_source *source;
        // Poll events. -1 means wait indefinitely until an event occurs
        if (ALooper_pollOnce(-1, NULL, &events, (void**)&source) >= 0) {
            if (source != NULL) source->process(app, source);
        }
    }

    if (!app->destroyRequested) {
        android_width = ANativeWindow_getWidth(app->window);
        android_height = ANativeWindow_getHeight(app->window);

        __android_log_print(ANDROID_LOG_INFO, "Doom", "Surface Size: %dx%d", android_width, android_height);
        SetupApplication();
        AndroidMakeFullscreen();
        doomgeneric_Create(1, argv);
    }
}

AAssetManager* GetAssetManager(void)
{
    if (gapp && gapp->activity)
        return gapp->activity->assetManager;
    return NULL;
}
