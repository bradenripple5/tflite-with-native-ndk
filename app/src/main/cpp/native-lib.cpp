#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/log.h>
#include "Renderer.h"
#include "NativeCamera.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NDK_MAIN", __VA_ARGS__)

static Renderer     gRenderer;
static NativeCamera gCamera;

static void onWindowCreated(ANativeActivity*, ANativeWindow* window) {
    long total = 0;
    long average_milliseconds = 0;
    int avg_index =0;
    LOGI("onWindowCreated -- nativelib");
    gRenderer.createProgram();  // ← MUST call this

    gRenderer.init(window);  // EGL + GL init
//    gCamera.open(640, 480, [](AImage* img) {
//        gRenderer.uploadYUV(img);   // sets frameReady_ = true
//        gRenderer.draw();           // only draw when ready
//    });

//
    gCamera.open(640, 480, [&total, &average_milliseconds, &avg_index, &window](AImage* img) {


        gRenderer.uploadYUV(img);
        gRenderer.draw();
        static struct timespec prev_time;
        struct timespec now;

        clock_gettime(CLOCK_MONOTONIC, &now);
        long diff_ms = (now.tv_sec - prev_time.tv_sec) * 1000 + (now.tv_nsec - prev_time.tv_nsec) / 1000000;
        total = total + diff_ms;
        average_milliseconds = total/avg_index;
        avg_index++;
        prev_time = now;

        LOGI("Frame diff: %ld ms, with average %ld",diff_ms, average_milliseconds);

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