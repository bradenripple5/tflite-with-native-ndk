#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <random>

#include <random>
#define LOG_TAG "NativeApp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

class NativeApp {
public:

    GLuint texY_ = 0, texU_ = 0, texV_ = 0;
    GLuint shaderProgram_ = 0;
    GLuint vbo_ = 0;

    const char* vertexShaderSrc = R"(
    attribute vec4 a_Position;
    attribute vec2 a_TexCoord;
    varying vec2 v_TexCoord;
    void main() {
        gl_Position = a_Position;
        v_TexCoord = a_TexCoord;
    }
)";

    const char* fragmentShaderSrc = R"(
    precision mediump float;
    varying vec2 v_TexCoord;
    uniform sampler2D texY;
    uniform sampler2D texU;
    uniform sampler2D texV;

    void main() {
        float y = texture2D(texY, v_TexCoord).r;
        float u = texture2D(texU, v_TexCoord).r - 0.5;
        float v = texture2D(texV, v_TexCoord).r - 0.5;

        float r = y + 1.402 * v;
        float g = y - 0.344136 * u - 0.714136 * v;
        float b = y + 1.772 * u;

        gl_FragColor = vec4(r, g, b, 1.0);
    }
)";


    void init(ANativeWindow* window) {
        LOGI("Initializing NativeApp...");
        if (!window){
            LOGE("no window in ANativeWindow");
            return;
        }

        window_ = window;   // ✅ store window
        initCamera();       // ✅ camera can still be done here
        startRenderLoop();  // ✅ EGL now happens in render thread only
    }


    void shutdown() {
        stopRenderLoop();
        closeCamera();
        destroyEGL();
    }

private:
    // EGL + GL
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLConfig  config_;

    void initEGL(ANativeWindow* window) {
        if (!window){
            LOGE("no window in initEGL(ANativeWindow");
        }
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(display_, nullptr, nullptr);
        const EGLint attribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                EGL_NONE
        };
        EGLint numConfigs;
        eglChooseConfig(display_, attribs, &config_, 1, &numConfigs);
        context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
        surface_ = eglCreateWindowSurface(display_, config_, window, nullptr);
        eglMakeCurrent(display_, surface_, surface_, context_);
    }

    void destroyEGL() {
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
            if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
            eglTerminate(display_);
        }
        display_ = EGL_NO_DISPLAY;
        surface_ = EGL_NO_SURFACE;
        context_ = EGL_NO_CONTEXT;
    }

    // Camera
    ACameraManager* manager_ = nullptr;
    ACameraDevice* camera_ = nullptr;
    ACameraCaptureSession* session_ = nullptr;
    ACaptureRequest* request_ = nullptr;
    AImageReader* reader_ = nullptr;
    ANativeWindow* readerWindow_ = nullptr;
    ACameraOutputTarget* outputTarget_ = nullptr;
    ACaptureSessionOutput* output_ = nullptr;
    ACaptureSessionOutputContainer* container_ = nullptr;
    ANativeWindow* window_ = nullptr;

    void initCamera() {
        manager_ = ACameraManager_create();
        AImageReader_new(640, 480, AIMAGE_FORMAT_YUV_420_888, 4, &reader_);

        AImageReader_ImageListener listener = {
                .context = this,
                .onImageAvailable = &NativeApp::onImageAvailableStatic
        };
        AImageReader_setImageListener(reader_, &listener);

        while (AImageReader_getWindow(reader_, &readerWindow_) != AMEDIA_OK || !readerWindow_) {
            usleep(10000);
        }

        ACameraIdList* ids;
        ACameraManager_getCameraIdList(manager_, &ids);
        const char* selected = ids->cameraIds[0];
        ACameraMetadata* metadata;
        ACameraManager_getCameraCharacteristics(manager_, selected, &metadata);
        ACameraMetadata_free(metadata);

        ACameraDevice_StateCallbacks deviceCb{};
        ACameraManager_openCamera(manager_, selected, &deviceCb, &camera_);

        ACameraDevice_createCaptureRequest(camera_, TEMPLATE_PREVIEW, &request_);
        ACameraOutputTarget_create(readerWindow_, &outputTarget_);
        ACaptureRequest_addTarget(request_, outputTarget_);

        ACaptureSessionOutputContainer_create(&container_);
        ACaptureSessionOutput_create(readerWindow_, &output_);
        ACaptureSessionOutputContainer_add(container_, output_);

        ACameraCaptureSession_stateCallbacks sessionCb{};
        ACameraDevice_createCaptureSession(camera_, container_, &sessionCb, &session_);
        ACameraCaptureSession_setRepeatingRequest(session_, nullptr, 1, &request_, nullptr);
    }

    void closeCamera() {
        if (session_) ACameraCaptureSession_close(session_);
        if (request_) ACaptureRequest_free(request_);
        if (outputTarget_) ACameraOutputTarget_free(outputTarget_);
        if (output_) ACaptureSessionOutput_free(output_);
        if (container_) ACaptureSessionOutputContainer_free(container_);
        if (reader_) AImageReader_delete(reader_);
        if (camera_) ACameraDevice_close(camera_);
        if (manager_) ACameraManager_delete(manager_);

        session_ = nullptr;
        request_ = nullptr;
        outputTarget_ = nullptr;
        output_ = nullptr;
        container_ = nullptr;
        reader_ = nullptr;
        camera_ = nullptr;
        manager_ = nullptr;
    }

    static void onImageAvailableStatic(void* ctx, AImageReader* reader) {
        reinterpret_cast<NativeApp*>(ctx)->onImageAvailable(reader);
    }

    void onImageAvailable(AImageReader* reader) {
        AImage* img = nullptr;
        if (AImageReader_acquireLatestImage(reader, &img) == AMEDIA_OK && img) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (latestImage_) AImage_delete(latestImage_);
            latestImage_ = img;
            cv_.notify_one();
        }
    }

    // Render thread
    std::thread renderThread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    AImage* latestImage_ = nullptr;

    void startRenderLoop() {
        running_ = true;
        renderThread_ = std::thread([this]() {
            if(!window_){
                LOGE(" no window_ in startRenerLoop before wihle loop");
            }
            initEGL(window_);  // <-- MOVE EGL creation here

            while (running_) {
                LOGI("startRenderLoop running");
                AImage* image = nullptr;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock, [this]() { return !running_ || latestImage_ != nullptr; });
                    if (!running_) break;

                    image = latestImage_;
                    latestImage_ = nullptr;
                }

                if (image) {
                    uploadYUV(image);  // ✅ Upload actual pixel data
                    draw();            // ✅ Then draw the frame
                    AImage_delete(image);
                }
            }

            destroyEGL();  // clean up EGL in the same thread
        });
    }


    void stopRenderLoop() {
        running_ = false;
        cv_.notify_all();
        if (renderThread_.joinable()) renderThread_.join();
    }

    void draw() {
        if (eglGetCurrentContext() != context_) {
            if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
                LOGE("draw: eglMakeCurrent failed: 0x%x", eglGetError());
                return;
            }
        }

#include <chrono>


        auto t0 = std::chrono::high_resolution_clock::now();

        glUseProgram(0);  // optional when not using shaders


        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<float> dist(0.0f, 1.0f);

// Example usage
        float r = dist(gen);
        float g = dist(gen);
        float b = dist(gen);

        glClearColor(r, g, b, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT);

        auto t1 = std::chrono::high_resolution_clock::now();
        auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        __android_log_print(ANDROID_LOG_INFO, "Timing", "glClear took %lld µs", elapsedUs);


        if (!eglSwapBuffers(display_, surface_)) {
            LOGE("draw: eglSwapBuffers failed: 0x%x", eglGetError());
        }
    }


    void uploadYUV(AImage* image) {
        int width, height;
        AImage_getWidth(image, &width);
        AImage_getHeight(image, &height);

        uint8_t *yPlane, *uPlane, *vPlane;
        int yLen, uLen, vLen;
        AImage_getPlaneData(image, 0, &yPlane, &yLen);
        AImage_getPlaneData(image, 1, &uPlane, &uLen);
        AImage_getPlaneData(image, 2, &vPlane, &vLen);

        int yStride, uStride, vStride;
        AImage_getPlaneRowStride(image, 0, &yStride);
        AImage_getPlaneRowStride(image, 1, &uStride);
        AImage_getPlaneRowStride(image, 2, &vStride);

        // Upload Y
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texY_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, yStride, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, yPlane);

        // Upload U
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texU_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, uStride, height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, uPlane);

        // Upload V
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, texV_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, vStride, height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, vPlane);
    }
    GLuint compileShader(GLenum type, const char* src) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        return shader;
    }

    void setupGL() {
        GLuint vtx = compileShader(GL_VERTEX_SHADER, vertexShaderSrc);
        GLuint frg = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc);
        shaderProgram_ = glCreateProgram();
        glAttachShader(shaderProgram_, vtx);
        glAttachShader(shaderProgram_, frg);
        glLinkProgram(shaderProgram_);
        glUseProgram(shaderProgram_);

        glGenTextures(1, &texY_);
        glGenTextures(1, &texU_);
        glGenTextures(1, &texV_);

        // Full-screen quad VBO
        const GLfloat quad[] = {
                -1, -1,  0, 1,
                1, -1,  1, 1,
                -1,  1,  0, 0,
                1,  1,  1, 0,
        };
        glGenBuffers(1, &vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

        GLint posLoc = glGetAttribLocation(shaderProgram_, "a_Position");
        GLint texLoc = glGetAttribLocation(shaderProgram_, "a_TexCoord");

        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(texLoc);
        glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        glUniform1i(glGetUniformLocation(shaderProgram_, "texY"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram_, "texU"), 1);
        glUniform1i(glGetUniformLocation(shaderProgram_, "texV"), 2);
    }


};


// Static instance
static NativeApp gApp;

extern "C" void ANativeActivity_onCreate(ANativeActivity* activity, void*, size_t) {
    activity->callbacks->onNativeWindowCreated = [](ANativeActivity*, ANativeWindow* window) {
        gApp.init(window);
    };

    activity->callbacks->onNativeWindowDestroyed = [](ANativeActivity*, ANativeWindow*) {
        gApp.shutdown();
    };
}
