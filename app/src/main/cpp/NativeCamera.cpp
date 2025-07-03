#include "NativeCamera.h"
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>
#include <android/log.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NativeCamera", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "NativeCamera", __VA_ARGS__)

bool NativeCamera::open(int w, int h, FrameCallback cb) {
    LOGE("native camera open called");
    frameCb_ = std::move(cb);

    // 1. Create camera manager
    mgr_ = ACameraManager_create();

        // 2. Get first available camera ID
    ACameraIdList* ids = nullptr;
    if (ACameraManager_getCameraIdList(mgr_, &ids) != ACAMERA_OK || !ids || ids->numCameras == 0) {
        LOGE("No cameras found.");
        return false;
    }
    std::string camIdStr = ids->cameraIds[0];
    ACameraManager_deleteCameraIdList(ids);

    // 3. Open camera with basic callbacks
    ACameraDevice_StateCallbacks callbacks = {
            .context = this,
            .onDisconnected = onCameraDisconnected,
            .onError = onCameraError
    };
    LOGE("level32");

    camera_status_t status = ACameraManager_openCamera(mgr_, camIdStr.c_str(), &callbacks, &device_);
    if (status != ACAMERA_OK) {
        LOGE("openCamera failed with status: %d", status);
        return false;
    }

    LOGE("level36");

    // 4. Create AImageReader
    if (AImageReader_new(w, h, AIMAGE_FORMAT_YUV_420_888, 4, &reader_) != AMEDIA_OK) {
        LOGE("Failed to create AImageReader.");
        return false;
    }
    LOGE("level42");
    // 5. Set image listener
    AImageReader_ImageListener listener = {
            .context = this,
            .onImageAvailable = onImage
    };
    AImageReader_setImageListener(reader_, &listener);

    // 6. Wait for reader window to become ready
    ANativeWindow* wnd = nullptr;
    const int maxWaitMs = 2000;
    int waited = 0;
    while ((AImageReader_getWindow(reader_, &wnd) != AMEDIA_OK || !wnd) && waited < maxWaitMs) {
        usleep(10000);  // wait 10 ms
        waited += 10;
    }
    if (!wnd) {
        LOGE("Timeout waiting for reader window.");
        return false;
    }

    // 7. Create output container
    ACaptureSessionOutputContainer_create(&outputs_);
    ACaptureSessionOutput_create(wnd, &output_);
    ACaptureSessionOutputContainer_add(outputs_, output_);

    // 8. Create target and request
    ACameraOutputTarget_create(wnd, &target_);
    ACameraDevice_createCaptureRequest(device_, TEMPLATE_PREVIEW, &req_);
    ACaptureRequest_addTarget(req_, target_);

    // 9. Create session
    ACameraCaptureSession_stateCallbacks scbs = {};
    if (ACameraDevice_createCaptureSession(device_, outputs_, &scbs, &session_) != ACAMERA_OK) {
        LOGE("Failed to create session.");
        return false;
    }

    // 10. Start streaming
    int seq = 0;
    if (ACameraCaptureSession_setRepeatingRequest(session_, nullptr, 1, &req_, &seq) != ACAMERA_OK) {
        LOGE("Failed to start repeating request.");
        return false;
    }

    LOGI("Camera opened and streaming started.");
    return true;
}

void NativeCamera::onImage(void* ctx, AImageReader* reader) {
    auto* self = static_cast<NativeCamera*>(ctx);
    AImage* img = nullptr;
    if (AImageReader_acquireLatestImage(reader, &img) == AMEDIA_OK && img) {
        self->frameCb_(img);
        AImage_delete(img);
    }
}

void NativeCamera::close() {
    if (session_) {
        ACameraCaptureSession_close(session_);
        session_ = nullptr;
    }
    if (req_) {
        ACaptureRequest_free(req_);
        req_ = nullptr;
    }
    if (target_) {
        ACameraOutputTarget_free(target_);
        target_ = nullptr;
    }
    if (output_) {
        ACaptureSessionOutput_free(output_);
        output_ = nullptr;
    }
    if (outputs_) {
        ACaptureSessionOutputContainer_free(outputs_);
        outputs_ = nullptr;
    }
    if (reader_) {
        AImageReader_delete(reader_);
        reader_ = nullptr;
    }
    if (device_) {
        ACameraDevice_close(device_);
        device_ = nullptr;
    }
    if (mgr_) {
        ACameraManager_delete(mgr_);
        mgr_ = nullptr;
    }
}
