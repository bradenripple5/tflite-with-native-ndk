// this runs

// grayscale_preview_es3.cpp
// OpenGL ES 3.0 grayscale YUV camera preview with dedicated render thread

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <media/NdkImageReader.h>
#include <camera/NdkCameraManager.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#endif

#define LOG_TAG "GrayscalePreview"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static ANativeWindow* window_ = nullptr;
static EGLDisplay display_ = EGL_NO_DISPLAY;
static EGLSurface surface_ = EGL_NO_SURFACE;
static EGLContext context_ = EGL_NO_CONTEXT;
static GLuint shaderProgram_ = 0, texY_ = 0, vbo_ = 0;
static ACameraManager* cameraManager_ = nullptr;
static ACameraDevice* cameraDevice_ = nullptr;
static ACameraCaptureSession* captureSession_ = nullptr;
static ACaptureRequest* request_ = nullptr;
static AImageReader* imageReader_ = nullptr;

static std::thread renderThread;
static std::mutex frameMutex;
static std::condition_variable frameCV;
static std::queue<std::pair<uint8_t*, int>> frameQueue;
static bool running = true;

const char* vertexShaderSrc = "#version 300 es\n"
                              "layout(location = 0) in vec4 a_Position;\n"
                              "layout(location = 1) in vec2 a_TexCoord;\n"
                              "out vec2 v_TexCoord;\n"
                              "void main() {\n"
                              "    gl_Position = a_Position;\n"
                              "    v_TexCoord = a_TexCoord;\n"
                              "}\n";

const char* fragmentShaderSrc = "#version 300 es\n"
                                "precision mediump float;\n"
                                "in vec2 v_TexCoord;\n"
                                "uniform sampler2D texY;\n"
                                "out vec4 fragColor;\n"
                                "void main() {\n"
                                "    float y = texture(texY, v_TexCoord).r;\n"
                                "    fragColor = vec4(y, y, y, 1.0);\n"
                                "}\n";

GLuint compile(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        LOGE("Shader compile failed: %s", log);
    }
    return shader;
}

void initGL() {
    GLuint vs = compile(GL_VERTEX_SHADER, vertexShaderSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentShaderSrc);
    shaderProgram_ = glCreateProgram();
    glAttachShader(shaderProgram_, vs);
    glAttachShader(shaderProgram_, fs);
    glLinkProgram(shaderProgram_);
    GLint linkOK = 0;
    glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &linkOK);
    if (!linkOK) {
        char log[512];
        glGetProgramInfoLog(shaderProgram_, 512, nullptr, log);
        LOGE("Program link failed: %s", log);
    }
    glUseProgram(shaderProgram_);

    const GLfloat quad[] = {
            -1, -1,  0, 1,
            1, -1,  1, 1,
            -1,  1,  0, 0,
            1,  1,  1, 0,
    };
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glGenTextures(1, &texY_);
    glBindTexture(GL_TEXTURE_2D, texY_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 640, 480, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glUniform1i(glGetUniformLocation(shaderProgram_, "texY"), 0);
}

void drawLoop() {
    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        LOGE("eglMakeCurrent failed in drawLoop: 0x%x", eglGetError());
        return;
    }
    initGL();
    while (running) {
        LOGE("running");
        std::unique_lock<std::mutex> lock(frameMutex);
        frameCV.wait(lock, [] { return !frameQueue.empty() || !running; });
        if (!running) break;
        auto [yPlane, width] = frameQueue.front();
        frameQueue.pop();
        lock.unlock();

        glClearColor(0.0, 0.0, 1.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texY_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, 480, GL_RED, GL_UNSIGNED_BYTE, yPlane);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        eglSwapBuffers(display_, surface_);
    }
}

void onImageAvailable(void* context, AImageReader* reader) {
    AImage* image = nullptr;
    if (AImageReader_acquireLatestImage(reader, &image) == AMEDIA_OK && image) {
        uint8_t* yPlane = nullptr;
        int yLen, width;
        AImage_getPlaneData(image, 0, &yPlane, &yLen);
        AImage_getWidth(image, &width);
        frameMutex.lock();
        frameQueue.emplace(yPlane, width);
        frameMutex.unlock();
        frameCV.notify_one();
        AImage_delete(image);
    }
}

void openCamera() {
    cameraManager_ = ACameraManager_create();
    ACameraIdList* cameraIds;
    ACameraManager_getCameraIdList(cameraManager_, &cameraIds);
    const char* camId = cameraIds->cameraIds[0];

    ACameraDevice_StateCallbacks stateCallbacks = {};
    ACameraManager_openCamera(cameraManager_, camId, &stateCallbacks, &cameraDevice_);

    AImageReader_new(640, 480, AIMAGE_FORMAT_YUV_420_888, 3, &imageReader_);
    AImageReader_ImageListener listener = { .context = nullptr, .onImageAvailable = onImageAvailable };
    AImageReader_setImageListener(imageReader_, &listener);

    ANativeWindow* readerWindow;
    AImageReader_getWindow(imageReader_, &readerWindow);

    ACameraDevice_createCaptureRequest(cameraDevice_, TEMPLATE_PREVIEW, &request_);
    ACameraOutputTarget* outputTarget;
    ACameraOutputTarget_create(readerWindow, &outputTarget);
    ACaptureRequest_addTarget(request_, outputTarget);

    ACaptureSessionOutputContainer* outputs;
    ACaptureSessionOutputContainer_create(&outputs);
    ACaptureSessionOutput* sessionOutput;
    ACaptureSessionOutput_create(readerWindow, &sessionOutput);
    ACaptureSessionOutputContainer_add(outputs, sessionOutput);

    ACameraCaptureSession_stateCallbacks sessionCallbacks = {};
    ACameraDevice_createCaptureSession(cameraDevice_, outputs, &sessionCallbacks, &captureSession_);
    ACameraCaptureSession_setRepeatingRequest(captureSession_, nullptr, 1, &request_, nullptr);
}

void initEGL(ANativeWindow* win) {
    window_ = win;
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
        LOGE("EGL initialization failed");
        return;
    }

    EGLConfig config;
    EGLint numConfigs;
    const EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8, EGL_NONE
    };
    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };

    eglChooseConfig(display_, attribs, &config, 1, &numConfigs);
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, ctxAttribs);
    surface_ = eglCreateWindowSurface(display_, config, window_, nullptr);
}

extern "C" void ANativeActivity_onCreate(ANativeActivity* activity, void*, size_t) {
    activity->callbacks->onNativeWindowCreated = [](ANativeActivity*, ANativeWindow* win) {
        initEGL(win);
        openCamera();
        renderThread = std::thread(drawLoop);
    };

    activity->callbacks->onNativeWindowDestroyed = [](ANativeActivity*, ANativeWindow*) {
        running = false;
        frameCV.notify_all();
        if (renderThread.joinable()) renderThread.join();
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        if (display_ != EGL_NO_DISPLAY) eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
        surface_ = EGL_NO_SURFACE;
        context_ = EGL_NO_CONTEXT;
    };
}
