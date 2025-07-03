//
// Created by brady on 7/3/25.
//

#ifndef MY_APPLICATION_NATIVECAMERAGPU_H
#define MY_APPLICATION_NATIVECAMERAGPU_H
// NativeCameraGPU.h
#pragma once

#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>
#include <android/hardware_buffer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <functional>

class NativeCameraGPU {
public:
    bool open(int width, int height, std::function<void(AHardwareBuffer*)> onFrame);
    void close();

private:
    ACameraDevice* device_ = nullptr;
    ACameraCaptureSession* session_ = nullptr;
    ACameraManager* cameraManager_ = nullptr;
    AImageReader* imageReader_ = nullptr;
    std::function<void(AHardwareBuffer*)> frameCallback_;

    static void onImageAvailable(void* ctx, AImageReader* reader);
};

#endif //MY_APPLICATION_NATIVECAMERAGPU_H
