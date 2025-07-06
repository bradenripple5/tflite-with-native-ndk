
// ===== native-lib.cpp =====
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/log.h>
#include "Renderer.h"
#include "NativeCamera.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NDK_MAIN", __VA_ARGS__)

static Renderer     gRenderer;
static NativeCamera gCamera;



static void onWindowCreated(ANativeActivity*, ANativeWindow* window) {
    gRenderer.init(window);
    gRenderer.startRenderLoop();
    gCamera.open(640, 480, [](AImage* img) {
        gRenderer.submitFrame(img);
    });
}

static void onWindowDestroyed(ANativeActivity*, ANativeWindow*) {
    gCamera.close();
    gRenderer.stopRenderLoop();
    gRenderer.shutdown();
}


//    gCamera.open(640, 480, [window](AImage* img) {
//        // Always ensure surface is valid before drawing
////        gRenderer.setWindow(window);  // will recreate surface if changed
////        gRenderer.ensureCurrent();
////        gRenderer.setWindow(window);
////
//////        gRenderer.uploadYUV(img);
////
////        // Check context is still current
////
//        static struct timespec prev_time;
//        struct timespec now;
//
//        clock_gettime(CLOCK_MONOTONIC, &now);
//        long diff_ms = (now.tv_sec - prev_time.tv_sec) * 1000 + (now.tv_nsec - prev_time.tv_nsec) / 1000000;
//        LOGI("Frame diff: %ld ms, ",diff_ms);
//
//        gRenderer.draw();
//    });
//}


//static void onWindowDestroyed(ANativeActivity*, ANativeWindow*) {
//    gCamera.close();
//    gRenderer.shutdown();
//}

extern "C"
void ANativeActivity_onCreate(ANativeActivity* activity, void*, size_t) {
    activity->callbacks->onNativeWindowCreated = onWindowCreated;
    activity->callbacks->onNativeWindowDestroyed = onWindowDestroyed;
}
// ===== Renderer.h =====
#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <media/NdkImage.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>

class Renderer {
public:

    bool ensureCurrent();
    void setWindow(ANativeWindow* window);
    bool init(ANativeWindow* window);
    void draw();
    void shutdown();

    void startRenderLoop();                 // NEW
    void stopRenderLoop();                  // NEW
    void submitFrame(AImage* image);        // NEW

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLConfig  config_;

    GLuint shaderProgram_ = 0;
    GLuint vbo_ = 0;
    GLint  positionAttrib_ = -1;

    int surfaceWidth_ = 0;
    int surfaceHeight_ = 0;


    std::thread renderThread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};

    AImage* latestImage_ = nullptr;  // Will hold most recent image
};


// ===== Renderer.cpp =====
// Updated Renderer.cpp to ensure EGL context and surface are current and safe before drawing

#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define LOG_TAG "Renderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)


bool Renderer::init(ANativeWindow* window) {
    // 1. Get EGL display
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        LOGE("❌ Failed to get EGL display");
        return false;
    }

    // 2. Initialize EGL
    if (!eglInitialize(display_, nullptr, nullptr)) {
        LOGE("❌ Failed to initialize EGL");
        return false;
    }

    // 3. Choose EGL config
    const EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_NONE
    };
    EGLint numConfigs;
    if (!eglChooseConfig(display_, configAttribs, &config_, 1, &numConfigs) || numConfigs == 0) {
        LOGE("❌ Failed to choose EGL config");
        return false;
    }

    // 4. Create EGL context
    const EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
    };
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs);
    if (context_ == EGL_NO_CONTEXT) {
        LOGE("❌ Failed to create EGL context");
        return false;
    }

    // 5. Create EGL window surface
    surface_ = eglCreateWindowSurface(display_, config_, window, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        LOGE("❌ Failed to create EGL surface");
        return false;
    }

    // 6. Make EGL context current
    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        EGLint err = eglGetError();
        LOGE("❌ eglMakeCurrent failed with error: 0x%x", err);
        return false;
    }

    // 7. (Optional) Query width/height or set viewport
    EGLint width, height;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);
    glViewport(0, 0, width, height);

    LOGI("✅ Renderer initialized: %dx%d", width, height);
    return true;
}

bool Renderer::ensureCurrent() {
    if (eglGetCurrentContext() != context_ || eglGetCurrentSurface(EGL_DRAW) != surface_) {
        if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
            EGLint err = eglGetError();
            LOGE("ensureCurrent: Failed to make EGL current again: 0x%x", err);
            return false;
        }
    }
    return true;
}

void Renderer::setWindow(ANativeWindow* window) {
    if (window == nullptr) return;

    if (surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface_);
    }

    surface_ = eglCreateWindowSurface(display_, config_, window, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        EGLint err = eglGetError();
        LOGE("setWindow: Failed to create new surface: 0x%x", err);
        return;
    }

    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        EGLint err = eglGetError();
        LOGE("setWindow: Failed to make new EGL surface current: 0x%x", err);
    }
}

/*
//void Renderer::draw() {
//    if (!ensureCurrent()) return;
//
//    EGLint w, h;
//    eglQuerySurface(display_, surface_, EGL_WIDTH, &w);
//    eglQuerySurface(display_, surface_, EGL_HEIGHT, &h);
//    LOGI("Drawing with surface size: %d x %d", w, h);
//
//    glViewport(0, 0, w, h);
//    glClearColor(1.0f, 0.0f, 0.0f, 1.0f); // Red screen
//    glClear(GL_COLOR_BUFFER_BIT);
//
//    if (!eglSwapBuffers(display_, surface_)) {
//        EGLint err = eglGetError();
//        LOGE("eglSwapBuffers failed with error: 0x%x", err);
//    }
//}

void Renderer::draw() {
    if (display_ == EGL_NO_DISPLAY || surface_ == EGL_NO_SURFACE) return;
    eglSwapBuffers(display_, surface_);

    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);  // red
    glClear(GL_COLOR_BUFFER_BIT);

    eglSwapBuffers(display_, surface_);
}
*/
void Renderer::submitFrame(AImage* image) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (latestImage_) {
        AImage_delete(latestImage_);  // Drop old image if present
    }
    latestImage_ = image;
    cv_.notify_one();
}


void Renderer::startRenderLoop() {
    running_ = true;
    renderThread_ = std::thread([this]() {
        while (running_) {
            AImage* image = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return !running_ || latestImage_ != nullptr; });

                if (!running_) break;

                image = latestImage_;
                latestImage_ = nullptr;  // Clear it once taken
            }

            if (image) {
                draw();  // You can change this to uploadYUV(image); draw(); etc.
                AImage_delete(image);
            }
        }
    });
}

void Renderer::stopRenderLoop() {
    running_ = false;
    cv_.notify_all();
    if (renderThread_.joinable())
        renderThread_.join();
}


void Renderer::draw() {
    if (display_ == EGL_NO_DISPLAY || surface_ == EGL_NO_SURFACE || context_ == EGL_NO_CONTEXT) {
        LOGE("draw: Missing EGL display, surface, or context");
        return;
    }

    // Make sure this thread is bound to the EGL context
    if (eglGetCurrentContext() != context_ || eglGetCurrentSurface(EGL_DRAW) != surface_) {
        if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
            EGLint err = eglGetError();
            LOGE("draw: eglMakeCurrent failed with error: 0x%x", err);
            return;
        }
    }

    // Get actual surface dimensions
    EGLint width = 0, height = 0;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);
    LOGI("draw: surface dimensions = %d x %d", width, height);

    // Set viewport and clear color
    glViewport(0, 0, width, height);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);  // Red background
    glClear(GL_COLOR_BUFFER_BIT);

    // Present frame
    if (!eglSwapBuffers(display_, surface_)) {
        EGLint err = eglGetError();
        LOGI("draw: eglSwapBuffers failed with error: 0x%x", err);
    } else {
        LOGI("draw: frame swapped successfully");
    }
}


void Renderer::shutdown() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        eglTerminate(display_);
    }

    display_ = EGL_NO_DISPLAY;
    context_ = EGL_NO_CONTEXT;
    surface_ = EGL_NO_SURFACE;
}

// ===== NativeCamera.h =====
#pragma once
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>
#include <functional>
#include "Renderer.h"


class NativeCamera {
public:
    using FrameCallback = std::function<void(AImage*)>;

    bool open(int width, int height, FrameCallback cb);
    void close();
    void onImage(void* ctx, AImageReader* reader);
    Renderer gRenderer;

private:
    static void onCameraDisconnected(void*, ACameraDevice*) {}
    static void onCameraError(void*, ACameraDevice*, int) {}

    FrameCallback frameCb_;
    ACameraManager* manager_ = nullptr;
    ACameraDevice* camera_ = nullptr;
    ACameraCaptureSession* session_ = nullptr;
    ACaptureRequest* request_ = nullptr;
    AImageReader* reader_ = nullptr;
    ANativeWindow* readerWindow_ = nullptr;
    ACaptureSessionOutputContainer* container_ = nullptr;
    ACaptureSessionOutput* output_ = nullptr;
    ACameraOutputTarget* outputTarget_ = nullptr;

    void setupCamera(int width, int height);
};

// ===== NativeCamera.cpp =====
#include "NativeCamera.h"
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>
#include <android/log.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NativeCamera", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "NativeCamera", __VA_ARGS__)


bool NativeCamera::open(int width, int height, FrameCallback cb) {
    LOGI("nativecame open");
    frameCb_ = cb;
    manager_ = ACameraManager_create();

    AImageReader_new(width, height, AIMAGE_FORMAT_YUV_420_888, 4, &reader_);
    AImageReader_ImageListener listener = {
            .context = this,
            .onImageAvailable = &NativeCamera::onImage
    };
    AImageReader_setImageListener(reader_, &listener);

    const int maxWaitMs = 2000;
    int waited = 0;
    while ((AImageReader_getWindow(reader_, &readerWindow_) != AMEDIA_OK || !readerWindow_) && waited < maxWaitMs) {
        usleep(10000);
        waited += 10;
    }
    if (!readerWindow_) {
        LOGE("\u274c Timeout: AImageReader window not ready");
        return false;
    }

    setupCamera(width, height);
    return true;
}

void NativeCamera::setupCamera(int width, int height) {
    ACameraIdList* cameraIdList = nullptr;
    ACameraManager_getCameraIdList(manager_, &cameraIdList);

    const char* selectedCameraId = nullptr;
    for (int i = 0; i < cameraIdList->numCameras; ++i) {
        ACameraMetadata* metadata = nullptr;
        ACameraManager_getCameraCharacteristics(manager_, cameraIdList->cameraIds[i], &metadata);

        ACameraMetadata_const_entry entry;
        if ((media_status_t)ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &entry) == AMEDIA_OK &&
            entry.data.u8[0] == ACAMERA_LENS_FACING_BACK) {
            selectedCameraId = cameraIdList->cameraIds[i];
            ACameraMetadata_free(metadata);
            break;
        }
        ACameraMetadata_free(metadata);
    }

    if (!selectedCameraId) {
        LOGE("No back-facing camera found");
        return;
    }


    ACameraDevice_StateCallbacks deviceCallbacks = {};
    camera_status_t status = ACameraManager_openCamera(manager_, selectedCameraId, &deviceCallbacks, &camera_);
    if (status != ACAMERA_OK) {
        LOGE("Failed to open camera, status: %d", status);
        return;
    }

    status = ACameraDevice_createCaptureRequest(camera_, TEMPLATE_PREVIEW, &request_);
    if (status != ACAMERA_OK) {
        LOGE("Failed to create capture request, status: %d", status);
        return;
    }

    ACameraOutputTarget_create(readerWindow_, &outputTarget_);
    ACaptureRequest_addTarget(request_, outputTarget_);

    ACaptureSessionOutputContainer_create(&container_);
    ACaptureSessionOutput_create(readerWindow_, &output_);
    ACaptureSessionOutputContainer_add(container_, output_);

    ACameraCaptureSession_stateCallbacks sessionCallbacks = {};
    status = ACameraDevice_createCaptureSession(camera_, container_, &sessionCallbacks, &session_);
    if (status != ACAMERA_OK) {
        LOGE("Failed to create capture session, status: %d", status);
        return;
    }

    status = ACameraCaptureSession_setRepeatingRequest(session_, nullptr, 1, &request_, nullptr);
    if (status != ACAMERA_OK) {
        LOGE("Failed to set repeating request, status: %d", status);
    }
}

extern Renderer gRenderer;

void NativeCamera::onImage(void* ctx, AImageReader* reader) {
    AImage* img = nullptr;
    if (AImageReader_acquireLatestImage(reader, &img) != AMEDIA_OK || !img)
        return;

    gRenderer.submitFrame(img); // ✅ CORRECT
}


//void NativeCamera::onImage(void* ctx, AImageReader* reader) {
//    LOGE("onImage triggered");
//    auto* self = static_cast<NativeCamera*>(ctx);
//    AImage* img = nullptr;
//
//    media_status_t status = AImageReader_acquireLatestImage(reader, &img);
//    if (status != AMEDIA_OK || !img) {
//        LOGE("\u274c Failed to acquire image, status: %d", status);
//        return;
//    }
//
//    if (self->frameCb_) {
//        self->frameCb_(img);
//    } else {
//        LOGE("frameCb_ is null");
//    }
//    AImage_delete(img);
//}

void NativeCamera::close() {
    if (session_) {
        ACameraCaptureSession_close(session_);
        session_ = nullptr;
    }
    if (request_) {
        ACaptureRequest_free(request_);
        request_ = nullptr;
    }
    if (outputTarget_) {
        ACameraOutputTarget_free(outputTarget_);
        outputTarget_ = nullptr;
    }
    if (output_) {
        ACaptureSessionOutput_free(output_);
        output_ = nullptr;
    }
    if (container_) {
        ACaptureSessionOutputContainer_free(container_);
        container_ = nullptr;
    }
    if (reader_) {
        AImageReader_delete(reader_);
        reader_ = nullptr;
    }
    if (camera_) {
        ACameraDevice_close(camera_);
        camera_ = nullptr;
    }
    if (manager_) {
        ACameraManager_delete(manager_);
        manager_ = nullptr;
    }
}
