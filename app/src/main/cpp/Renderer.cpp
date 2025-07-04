

//here's my renderer.cpp


#include "Renderer.h"
#include <android/log.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include "string"
#include <android/log.h>
#include <unistd.h>
#include <EGL/egl.h>

#define TAG "Renderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)


namespace {
    const char* VERT_SHADER = R"(
    attribute vec4 aPosition;
    attribute vec2 aTexCoord;
    varying vec2 vTexCoord;
    void main() {
        gl_Position = aPosition;
        vTexCoord = aTexCoord;
    }
)";

    const char* FRAG_SHADER = R"(
    precision mediump float;
    varying vec2 vTexCoord;
    uniform sampler2D texY;
    uniform sampler2D texU;
    uniform sampler2D texV;

    void main() {
        float y = texture2D(texY, vTexCoord).r;
        float u = texture2D(texU, vTexCoord).r - 0.5;
        float v = texture2D(texV, vTexCoord).r - 0.5;
        float r = y + 1.402 * v;
        float g = y - 0.344 * u - 0.714 * v;
        float b = y + 1.772 * u;
        gl_FragColor = vec4(r, g, b, 1.0);
    }
)";

    const GLfloat QUAD[] = {
            // X,   Y,   U,   V
            -1.f, -1.f, 0.f, 1.f,
            1.f, -1.f, 1.f, 1.f,
            -1.f,  1.f, 0.f, 0.f,
            1.f,  1.f, 1.f, 0.f,
    };
}

bool Renderer::init(ANativeWindow* window) {
    eglInitialize(display_, nullptr, nullptr);
    if (display_ == EGL_NO_DISPLAY) {
        LOGE("Failed to get EGL display");
        return false;
    }
    if (!eglInitialize(display_, nullptr, nullptr)) {
        LOGE("Failed to initialize EGL");
        return false;
    }

    const EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(display_, configAttribs, &config_, 1, &numConfigs);
        if (config_ == nullptr) {
            LOGE("DRAWCHK config_ is null!");
            return false;
        }
    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs);  // ✅
    surface_ = eglCreateWindowSurface(display_, config_, window, nullptr);
    if (context_ == EGL_NO_CONTEXT) {
        LOGE("Failed to create EGL context");
        return false;
    }

    if (surface_ == EGL_NO_SURFACE) {
        LOGE("Failed to create EGL surface");
        return false;
    }

    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        EGLint eglError = eglGetError();
        LOGE("Failed to make EGL context current, EGL error: %x", eglError);
        return false;
    }
    LOGE("returninig rendinit true");
    return true;
}

bool Renderer::createProgram() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, VERT_SHADER);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FRAG_SHADER);
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    return true;
}



bool Renderer::recreateSurface(ANativeWindow* window) {
    if (surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface_);
    }

    if (window == nullptr) {
        LOGE("recreateSurface: window is null!");
        return false;
    }

    surface_ = eglCreateWindowSurface(display_, config_, window, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        EGLint err = eglGetError();
        LOGE("recreateSurface: Failed to create surface, EGL error: 0x%x", err);
        return false;
    }

    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        LOGE("recreateSurface: Failed to make EGL context current");
        return false;
    }

    LOGI("recreateSurface: Surface successfully recreated.");
    return true;
}


GLuint Renderer::compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    return shader;
}

void Renderer::createTextures(int w, int h) {
    glGenTextures(1, &texY_);
    glGenTextures(1, &texU_);
    glGenTextures(1, &texV_);

    auto setupTex = [](GLuint tex, int width, int height) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    };
    setupTex(texY_, w, h);
    setupTex(texU_, w / 2, h / 2);
    setupTex(texV_, w / 2, h / 2);
}
void logPlaneBitsInRenderer(AImage* img, uint8_t* plane) {

//    uint8_t* plane = nullptr;
    int yLen = 0;
    media_status_t status = AImage_getPlaneData(img, 0, &plane, &yLen);
    LOGE("getPlaneData DEFG status: %d, plane: %p, yLen: %d", status, plane, yLen);

    if (AImage_getPlaneData(img, 0, &plane, &yLen) != AMEDIA_OK || !plane || yLen <= 0) {
        LOGE("❌ Failed to get Y plane");
        return ;
    }

    // Log first N bytes in binary
    const int N = 64; // limit for sanity
    std::string bitDump = "U plane first 64 bytes as bits:\n";
    LOGE("yplane in renderer 0 = %i",plane[0]);
//    for (int i = 0; i < std::min(N, yLen); ++i) {
//        for (int b = 7; b >= 0; --b) {
//            LOGE("yplane[%i] = %i",b, yPlane[b]);
//            bitDump += (yPlane[i] & (1 << b)) ? '1' : '0';
//        }
//        bitDump += ' ';
//    }

    LOGE("current bitdump = %s", bitDump.c_str());
}

void Renderer::uploadYUV(AImage* image) {

    for (int i = 0; i<2;i++){
        LOGI("uploadYUV YUVYUVYUVYUVYUVYUV");
    }


    int yStride, uvStride;
    uint8_t* yPlane = nullptr;
    uint8_t* uPlane = nullptr;
    uint8_t* vPlane = nullptr;
    int yLen, uLen, vLen;
    int width, height;

    AImage_getWidth(image, &width);
    AImage_getHeight(image, &height);

    AImage_getPlaneRowStride(image, 0, &yStride);
    AImage_getPlaneData(image, 0, &yPlane, &yLen);
    AImage_getPlaneData(image, 1, &uPlane, &uLen);
    AImage_getPlaneData(image, 2, &vPlane, &vLen);
    AImage_getPlaneRowStride(image, 1, &uvStride);

    createTextures(width, height);

    glBindTexture(GL_TEXTURE_2D, texY_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, yPlane);

    glBindTexture(GL_TEXTURE_2D, texU_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, uPlane);

    glBindTexture(GL_TEXTURE_2D, texV_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, vPlane);

    logPlaneBitsInRenderer(image, uPlane);

    LOGI("Window size: %d x %d", width, height);

}

/*
void Renderer::draw() {
//    int wait_ms = 0;
//    while (!frameReady_ && wait_ms < 100) {
//        usleep(1000);  // sleep 1ms
//        wait_ms++;
//    }
//
//    if (!frameReady_) {
//        LOGE("Still not ready after 100ms wait");
//        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);  // Red
//        glClear(GL_COLOR_BUFFER_BIT);
//        eglSwapBuffers(display_, surface_);
//        return;
//    }


    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(prog_);

    GLint posLoc = glGetAttribLocation(prog_, "aPosition");
    GLint texLoc = glGetAttribLocation(prog_, "aTexCoord");

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(texLoc);

// Bind YUV textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texY_);
    glUniform1i(glGetUniformLocation(prog_, "texY"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texU_);
    glUniform1i(glGetUniformLocation(prog_, "texU"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texV_);
    glUniform1i(glGetUniformLocation(prog_, "texV"), 2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(display_, surface_);


    glViewport(0, 0, 1280, 720);  // optionally get window size dynamically
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);  // Red
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(display_, surface_);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);  // Red

//    glClear(GL_COLOR_BUFFER_BIT);
//
//    glUseProgram(prog_);
//
//    GLint posLoc = glGetAttribLocation(prog_, "aPosition");
//    GLint texLoc = glGetAttribLocation(prog_, "aTexCoord");
//
//    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
//    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
//    glEnableVertexAttribArray(posLoc);
//    glVertexAttribPointer(texLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
//    glEnableVertexAttribArray(texLoc);
//
//    glActiveTexture(GL_TEXTURE0);
//    glBindTexture(GL_TEXTURE_2D, texY_);
//    glUniform1i(glGetUniformLocation(prog_, "texY"), 0);
//
//    glActiveTexture(GL_TEXTURE1);
//    glBindTexture(GL_TEXTURE_2D, texU_);
//    glUniform1i(glGetUniformLocation(prog_, "texU"), 1);
//
//    glActiveTexture(GL_TEXTURE2);
//    glBindTexture(GL_TEXTURE_2D, texV_);
//    glUniform1i(glGetUniformLocation(prog_, "texV"), 2);
//
//    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
//    eglSwapBuffers(display_, surface_);
}

void Renderer::draw() {

    EGLint width, height;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);
    LOGI("Surface dimensions: %d x %d", width, height);


    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        LOGE("Failed to get EGL display");
    }

    if (!eglInitialize(display_, nullptr, nullptr)) {
        LOGE("Failed to initialize EGL");
    }

    if (display_ == EGL_NO_DISPLAY) {
        LOGE("EGL_NO_DISPLAY: Unable to initialize EGL.");
        return;
    }
    if (surface_ == EGL_NO_SURFACE) {
        LOGE("EGL_NO_SURFACE: Unable to create surface.");
        return;
    }
    if (context_ == EGL_NO_CONTEXT) {
        LOGE("EGL_NO_CONTEXT: Unable to create EGL context.");
        return;
    }
    LOGE("YO MAMA");
    // Clear the screen with a red color
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);  // Red color
    glClear(GL_COLOR_BUFFER_BIT);

    // Swap buffers to display the red color
    eglSwapBuffers(display_, surface_);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOGE("OpenGL Error: %x", err);
    }

}



void Renderer::draw(ANativeWindow* window) {

    EGLConfig config;

//    if (surface_ == EGL_NO_SURFACE || !eglMakeCurrent(display_, surface_, surface_, context_)) {
//        LOGE("Reinitializing EGL context and surface");
//        // Recreate the surface and rebind the context
//        surface_ = eglCreateWindowSurface(display_, config, window, nullptr);
//        eglMakeCurrent(display_, surface_, surface_, context_);
//    }
    const GLfloat QUAD[] = {
            -1.f, -1.f, 0.f, 1.f,
            1.f, -1.f, 1.f, 1.f,
            -1.f,  1.f, 0.f, 0.f,
            1.f,  1.f, 1.f, 0.f,
    };

    GLuint aPos = glGetAttribLocation(prog_, "aPosition");
    GLuint aTex = glGetAttribLocation(prog_, "aTexCoord");

    glEnableVertexAttribArray(aPos);
    glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), QUAD);

    glEnableVertexAttribArray(aTex);
    glVertexAttribPointer(aTex, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), QUAD + 2);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);  // Red color
    glClear(GL_COLOR_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texY_);
    glUniform1i(glGetUniformLocation(prog_, "texY"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texU_);
    glUniform1i(glGetUniformLocation(prog_, "texU"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texV_);
    glUniform1i(glGetUniformLocation(prog_, "texV"), 2);

    // Set the OpenGL viewport to match the window size
    int width = 1080;  // or dynamically get it
    int height = 2340; // or dynamically get it
    glViewport(0, 0, width, height);  // Set the viewport to the surface dimensions
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Swap the buffers to display the red color
//    eglSwapBuffers(display_, surface_);
    if (eglSwapBuffers(display_, surface_) == EGL_FALSE) {
        EGLint eglError = eglGetError();  // Get the specific EGL error
        LOGE("eglSwapBuffers failed, EGL error: %x", eglError);  // Log the error code
    }
//    LOGE("prog nullcheck = %b",(prog_ == nullptr));
    glUseProgram(prog_);

}
*/


void Renderer::draw() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glViewport(0, 0, 1080, 2340);  // TODO: Replace with actual surface size if dynamic

    glUseProgram(prog_);

    GLuint aPos = glGetAttribLocation(prog_, "aPosition");
    GLuint aTex = glGetAttribLocation(prog_, "aTexCoord");

    glEnableVertexAttribArray(aPos);
    glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), QUAD);

    glEnableVertexAttribArray(aTex);
    glVertexAttribPointer(aTex, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), QUAD + 2);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texY_);
    glUniform1i(glGetUniformLocation(prog_, "texY"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texU_);
    glUniform1i(glGetUniformLocation(prog_, "texU"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texV_);
    glUniform1i(glGetUniformLocation(prog_, "texV"), 2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
        LOGE("DRAWCHK Reinitializing EGL context and surface");

        // Destroy old surface
        if (surface_ != EGL_NO_SURFACE) {
            LOGE("DRAWCHK surface_ != EGL_NO_SURFACE  ");
            eglDestroySurface(display_, surface_);
        }

        // Recreate surface
//        surface_ = eglCreateWindowSurface(display_, config_, window, nullptr);
        if (surface_ == EGL_NO_SURFACE) {
            LOGE("DRAWCHK Failed to recreate EGL surface");
            return;
        }

        if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
            LOGE("DRAWCHK Failed to make EGL context current after surface recreation");
            return;
        }
    }

    if (eglSwapBuffers(display_, surface_) == EGL_FALSE) {
        EGLint eglError = eglGetError();
        LOGE("DRAWCHK eglSwapBuffers failed, EGL error: %x", eglError);
    }
}


void Renderer::shutdown() {
    glDeleteBuffers(1, &vbo_);
    glDeleteProgram(prog_);
    glDeleteTextures(1, &texY_);
    glDeleteTextures(1, &texU_);
    glDeleteTextures(1, &texV_);

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
//
// Created by brady on 6/30/25.
//
