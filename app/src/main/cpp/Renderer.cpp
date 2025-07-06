// Updated Renderer.cpp to ensure EGL context and surface are current and safe before drawing

#include "Renderer.h"
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
