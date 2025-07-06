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


        int32_t fpsRange[2] = {55, 60};  // constant 30 FPS
        camera_status_t fpsResult = ACaptureRequest_setEntry_i32(
                request_,
                ACAMERA_CONTROL_AE_TARGET_FPS_RANGE,
                2,
                fpsRange
        );

        if (fpsResult != ACAMERA_OK) {
            LOGE("Failed to set FPS range: %d", fpsResult);
        }

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
        if (!window) {
            LOGE("initEGL: window is null");
            return;
        }

        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY) {
            LOGE("initEGL: Unable to get EGL display");
            return;
        }

        if (!eglInitialize(display_, nullptr, nullptr)) {
            LOGE("initEGL: eglInitialize failed");
            return;
        }

        const EGLint attribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_DEPTH_SIZE, 16,
                EGL_NONE
        };

        EGLint numConfigs;
        if (!eglChooseConfig(display_, attribs, &config_, 1, &numConfigs) || numConfigs == 0) {
            LOGE("initEGL: eglChooseConfig failed");
            return;
        }

        surface_ = eglCreateWindowSurface(display_, config_, window, nullptr);
        if (surface_ == EGL_NO_SURFACE) {
            LOGE("initEGL: eglCreateWindowSurface failed");
            return;
        }

        const EGLint ctxAttribs[] = {
                EGL_CONTEXT_CLIENT_VERSION, 2,
                EGL_NONE
        };
        context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, ctxAttribs);
        if (context_ == EGL_NO_CONTEXT) {
            LOGE("initEGL: eglCreateContext failed");
            return;
        }

        if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
            LOGE("initEGL: eglMakeCurrent failed");
            return;
        }

        // Set viewport to surface size
        EGLint width = 0, height = 0;
        eglQuerySurface(display_, surface_, EGL_WIDTH, &width);
        eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);
        glViewport(0, 0, width, height);
        LOGI("initEGL: Viewport set to %d x %d", width, height);
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
        AImageReader_new(640, 480, AIMAGE_FORMAT_YUV_420_888, 3, &reader_);

        AImageReader_ImageListener listener = {
                .context = this,
                .onImageAvailable = &NativeApp::onImageAvailableStatic
        };
        AImageReader_setImageListener(reader_, &listener);

//        while (AImageReader_getWindow(reader_, &readerWindow_) != AMEDIA_OK || !readerWindow_) {
//            usleep(10000);
//        }
        media_status_t winStat = AImageReader_getWindow(reader_, &readerWindow_);
        if (winStat != AMEDIA_OK || !readerWindow_) {
            LOGE("Failed to get AImageReader window: %d", winStat);
            return;
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


    void onImageAvailable(AImageReader* reader) {
        AImage* img = nullptr;
        media_status_t status = AImageReader_acquireLatestImage(reader, &img);
        if (status != AMEDIA_OK || !img) {
            LOGE("Failed to acquire image: %d", status);
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (latestImage_) AImage_delete(latestImage_);
        latestImage_ = img;
        cv_.notify_one();
    }


    static void onImageAvailableStatic(void* ctx, AImageReader* reader) {
        reinterpret_cast<NativeApp*>(ctx)->onImageAvailable(reader);
    }

//    void onImageAvailable(AImageReader* reader) {
//        AImage* img = nullptr;
//        if (AImageReader_acquireLatestImage(reader, &img) == AMEDIA_OK && img) {
//            std::lock_guard<std::mutex> lock(mutex_);
//            if (latestImage_) AImage_delete(latestImage_);
//            latestImage_ = img;
//            cv_.notify_one();
//        }
//    }

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
            bool initialized_ = false;




            while (running_) {



                auto t0 = std::chrono::high_resolution_clock::now();

                glUseProgram(shaderProgram_);  // optional when not using shaders


                static std::random_device rd;
                static std::mt19937 gen(rd());
                static std::uniform_real_distribution<float> dist(0.0f, 1.0f);

//
                AImage* image = nullptr;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock, [this]() { return !running_ || latestImage_ != nullptr; });
                    if (!running_) break;

                    image = latestImage_;
                    latestImage_ = nullptr;
                }

                if (image) {
                    if (!initialized_) {
                        setupGL(image);
                        initialized_ = true;
                    }

                    uploadYUV(image);
                    draw();
                    AImage_delete(image);
                }

                auto t1 = std::chrono::high_resolution_clock::now();
                auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
                __android_log_print(ANDROID_LOG_INFO, "Timing", "full render loop took %lld µs", elapsedUs);
                LOGI("startRenderLoop running");
            }

            destroyEGL();  // clean up EGL in the same thread
        });
    }


    void stopRenderLoop() {
        running_ = false;
        cv_.notify_all();
        if (renderThread_.joinable()) renderThread_.join();
    }
/*
    void draw() {

        if (eglGetCurrentContext() != context_) {
            if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
                LOGE("draw: eglMakeCurrent failed: 0x%x", eglGetError());
                return;
            }
        }




        glUseProgram(shaderProgram_);


        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<float> dist(0.0f, 1.0f);

// Example usage
        float r = dist(gen);
        float g = dist(gen);
        float b = dist(gen);

        glClearColor(r, g, b, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT);

        if (!eglSwapBuffers(display_, surface_)) {
            LOGE("draw: eglSwapBuffers failed: 0x%x", eglGetError());
        }
    }
/*
 void draw() {
     if (eglGetCurrentContext() != context_) {
         if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
             LOGE("draw: eglMakeCurrent failed: 0x%x", eglGetError());
             return;
         }
     }return

     glUseProgram(shaderProgram_);

     glClear(GL_COLOR_BUFFER_BIT);

     glBindBuffer(GL_ARRAY_BUFFER, vbo_);

     GLint posLoc = glGetAttribLocation(shaderProgram_, "a_Position");
     GLint texLoc = glGetAttribLocation(shaderProgram_, "a_TexCoord");

     glEnableVertexAttribArray(posLoc);
     glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
     glEnableVertexAttribArray(texLoc);
     glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

     // Bind textures
     glActiveTexture(GL_TEXTURE0);
     glBindTexture(GL_TEXTURE_2D, texY_);
     glActiveTexture(GL_TEXTURE1);
     glBindTexture(GL_TEXTURE_2D, texU_);
     glActiveTexture(GL_TEXTURE2);
     glBindTexture(GL_TEXTURE_2D, texV_);

     // Draw 2 triangles (quad)
     glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

     if (!eglSwapBuffers(display_, surface_)) {
         LOGE("draw: eglSwapBuffers failed: 0x%x", eglGetError());
     }
 }
*/

    void draw() {
        if (eglGetCurrentContext() != context_) {
            if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
                LOGE("draw: eglMakeCurrent failed: 0x%x", eglGetError());
                return;
            }
        }

        glUseProgram(shaderProgram_);

        glClear(GL_COLOR_BUFFER_BIT);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        GLint posLoc = glGetAttribLocation(shaderProgram_, "a_Position");
        GLint texLoc = glGetAttribLocation(shaderProgram_, "a_TexCoord");

        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(texLoc);
        glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        // Bind YUV textures to respective texture units
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texY_);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texU_);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, texV_);

        // Draw fullscreen quad
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        if (!eglSwapBuffers(display_, surface_)) {
            LOGE("draw: eglSwapBuffers failed: 0x%x", eglGetError());
        }
    }




    void uploadYUV(AImage* image) {
        int width, height;
        AImage_getWidth(image, &width);
        AImage_getHeight(image, &height);

        uint8_t *yPlane = nullptr, *uPlane = nullptr, *vPlane = nullptr;
        int yLen, uLen, vLen;
        AImage_getPlaneData(image, 0, &yPlane, &yLen);
        AImage_getPlaneData(image, 1, &uPlane, &uLen);
        AImage_getPlaneData(image, 2, &vPlane, &vLen);

        if (!yPlane || !uPlane || !vPlane) {
            LOGE("One or more YUV planes are null.");
            return;
        }

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // very important for YUV alignment

        // Upload Y
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texY_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE, yPlane);

        // Upload U
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texU_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, uPlane);

        // Upload V
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, texV_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, vPlane);
    }
    GLuint compileShader(GLenum type, const char* src) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        return shader;
    }

    void setupGL(AImage* image) {
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

        int width, height;
        AImage_getWidth(image, &width);
        AImage_getHeight(image, &height);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // ensure proper byte alignment

        // Y plane texture
        glBindTexture(GL_TEXTURE_2D, texY_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // U and V plane textures (half resolution)
        glBindTexture(GL_TEXTURE_2D, texU_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width / 2, height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindTexture(GL_TEXTURE_2D, texV_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width / 2, height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Fullscreen quad (NDC coords + UVs)
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


