#include "NativeCameraGPU.h"
#include <android/log.h>

#define LOG_TAG "NativeCameraGPU"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool NativeCameraGPU::open(int width, int height, std::function<void(AHardwareBuffer*)> onFrame) {
    cameraManager_ = ACameraManager_create();
    frameCallback_ = onFrame;

    // Get camera ID
    ACameraIdList* idList = nullptr;
    if (ACameraManager_getCameraIdList(cameraManager_, &idList) != ACAMERA_OK || idList->numCameras < 1) {
        LOGE("No cameras available");
        return false;
    }

    const char* cameraId = idList->cameraIds[0];
    ACameraDevice_StateCallbacks deviceCallbacks = {};

    if (ACameraManager_openCamera(cameraManager_, cameraId, &deviceCallbacks, &device_) != ACAMERA_OK) {
        LOGE("Failed to open camera");
        return false;
    }

    ACameraManager_deleteCameraIdList(idList);

    // Create AImageReader with GPU usage
    if (AImageReader_newWithUsage(width, height, AIMAGE_FORMAT_YUV_420_888,
                                  AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, 3, &imageReader_) != AMEDIA_OK) {
        LOGE("Failed to create AImageReader");
        return false;
    }

    AImageReader_ImageListener listener = {
            .context = this,
            .onImageAvailable = onImageAvailable
    };
    AImageReader_setImageListener(imageReader_, &listener);

    // Skipping full session setup (add repeating request etc)
    // TODO: You must integrate session + request logic like in NativeCamera

    LOGI("Camera opened and image reader set up");
    return true;
}

void NativeCameraGPU::onImageAvailable(void* ctx, AImageReader* reader) {
    NativeCameraGPU* self = reinterpret_cast<NativeCameraGPU*>(ctx);
    AImage* image = nullptr;

    if (AImageReader_acquireNextImage(reader, &image) != AMEDIA_OK || !image) {
        LOGE("Failed to acquire image");
        return;
    }

    AHardwareBuffer* buffer = nullptr;
    if (AImage_getHardwareBuffer(image, &buffer) == AMEDIA_OK && buffer) {
        self->frameCallback_(buffer);
    } else {
        LOGE("Failed to get AHardwareBuffer");
    }

    AImage_delete(image);
}

void NativeCameraGPU::close() {
    if (device_) {
        ACameraDevice_close(device_);
        device_ = nullptr;
    }

    if (imageReader_) {
        AImageReader_delete(imageReader_);
        imageReader_ = nullptr;
    }

    if (cameraManager_) {
        ACameraManager_delete(cameraManager_);
        cameraManager_ = nullptr;
    }
}
