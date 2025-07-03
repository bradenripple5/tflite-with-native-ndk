#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/log.h>
#include "Renderer.h"
#include "NativeCamera.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NDK_MAIN", __VA_ARGS__)

static Renderer     gRenderer;
static NativeCamera gCamera;

static void onWindowCreated(ANativeActivity*, ANativeWindow* window) {
    LOGI("onWindowCreated -- nativelib");
    gRenderer.init(window);  // EGL + GL init
    gCamera.open(320, 480/2, [](AImage* img) {
        gRenderer.uploadYUV(img);
        gRenderer.draw();
        static struct timespec prev_time;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long diff_ms = (now.tv_sec - prev_time.tv_sec) * 1000 + (now.tv_nsec - prev_time.tv_nsec) / 1000000;
        prev_time = now;

        LOGI("Frame diff: %ld ms", diff_ms);

    });
}

static void onWindowDestroyed(ANativeActivity*, ANativeWindow*) {
    gCamera.close();
    gRenderer.shutdown();
}

extern "C"
void ANativeActivity_onCreate(ANativeActivity* activity, void*, size_t) {
    activity->callbacks->onNativeWindowCreated = onWindowCreated;
    activity->callbacks->onNativeWindowDestroyed = onWindowDestroyed;
}
