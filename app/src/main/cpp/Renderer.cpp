

//here's my renderer.cpp


#include "Renderer.h"
#include <android/log.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include "string"
#include <android/log.h>

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

    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        for (int i=0;i<100;i++){
            LOGE("EGL_NO_DISPLAY");

        }
        return false;
    }
    eglInitialize(display_, nullptr, nullptr);

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
    eglChooseConfig(display_, configAttribs, &config, 1, &numConfigs);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttribs);
    surface_ = eglCreateWindowSurface(display_, config, window, nullptr);
    eglMakeCurrent(display_, surface_, surface_, context_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);

    return createProgram();
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

void Renderer::uploadYUV(AImage* image) {

    for (int i = 0; i<100;i++){
        LOGI("uploadYUV momochron");
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
}

void Renderer::draw() {
    if (!frameReady_) {
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);  // Red
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(display_, surface_);
        return;
    }
    for (int i = 0; i<100; i++){
        LOGE("Renderer::draw");
    }
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


//    glViewport(0, 0, 1280, 720);  // optionally get window size dynamically
//    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);  // Red
//    glClear(GL_COLOR_BUFFER_BIT);
//    eglSwapBuffers(display_, surface_);
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
