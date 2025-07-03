//
// Created by brady on 6/22/25.
//

#ifndef MY_APPLICATION_RENDERER_H
#define MY_APPLICATION_RENDERER_H
// Renderer.h
#pragma once
#include <android/native_window.h>
#include <media/NdkImage.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

/**
 * Extremely small helper that:
 *  - sets up EGL on the window
 *  - compiles a simple YUV420 → RGB shader
 *  - uploads planes from AImage
 *  - draws a full-screen quad
 *
 * NOT production-ready (no error handling, no mipmaps, no rotation),
 * but enough to get a live preview.
 */
class Renderer {
public:
    Renderer()  = default;
    ~Renderer() { shutdown(); }

    /** Create EGL context & framebuffer for the given window */
    bool init(ANativeWindow* window);

    /** Convert camera AImage (YUV_420_888) into three GL_LUMINANCE textures */
    void uploadYUV(AImage* image);

    /** Draw the most-recently uploaded frame */
    void draw(ANativeWindow* window);

    /** Destroy GL resources + EGL surface/context */
    void shutdown();

private:
    bool frameReady_ = false;
    EGLDisplay  display_  = EGL_NO_DISPLAY;
    EGLSurface  surface_  = EGL_NO_SURFACE;
    EGLContext  context_  = EGL_NO_CONTEXT;
    GLuint      prog_     = 0;
    GLuint      texY_     = 0;
    GLuint      texU_     = 0;
    GLuint      texV_     = 0;
    GLuint      vbo_      = 0;

    bool createProgram();
    GLuint compileShader(GLenum type, const char* src);
    void   createTextures(int w, int h);
};

#endif //MY_APPLICATION_RENDERER_H
