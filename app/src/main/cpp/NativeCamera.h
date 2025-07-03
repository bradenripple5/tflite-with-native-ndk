#pragma once
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>
#include <functional>

class NativeCamera {
public:
    using FrameCallback = std::function<void(AImage*)>;

    bool open(int width, int height, FrameCallback cb);
    void close();

private:
    static void onImage(void* ctx, AImageReader* reader);
    static void onCameraDisconnected(void*, ACameraDevice*) {}
    static void onCameraError(void*, ACameraDevice*, int) {}

    FrameCallback frameCb_;
    ACameraManager* mgr_ = nullptr;
    ACameraDevice* device_ = nullptr;
    ACameraCaptureSession* session_ = nullptr;
    ACaptureRequest* req_ = nullptr;
    AImageReader* reader_ = nullptr;
    ACaptureSessionOutputContainer* outputs_ = nullptr;
    ACaptureSessionOutput* output_ = nullptr;
    ACameraOutputTarget* target_ = nullptr;
};
