//
// Created by vinsentli@tencent.com on 2025/3/10.
//

#pragma once
#include <EGL/egl.h>
#include <memory>

struct ARect;
struct AHardwareBuffer;
struct AHardwareBuffer_Desc;

#ifdef __cplusplus
extern "C" {
#endif
using PFAHardwareBuffer_allocate = int (*)(const AHardwareBuffer_Desc* desc,
                                            AHardwareBuffer** outBuffer);
using PFAHardwareBuffer_acquire = void (*)(AHardwareBuffer* buffer);
using PFAHardwareBuffer_describe = void (*)(const AHardwareBuffer* buffer,
                                            AHardwareBuffer_Desc* outDesc);
using PFAHardwareBuffer_lock = int (*)(AHardwareBuffer* buffer,
                                       uint64_t usage,
                                       int32_t fence,
                                       const ARect* rect,
                                       void** outVirtualAddress);
using PFAHardwareBuffer_recvHandleFromUnixSocket =
        int (*)(int socketFd, AHardwareBuffer** outBuffer);
using PFAHardwareBuffer_release = void (*)(AHardwareBuffer* buffer);
using PFAHardwareBuffer_sendHandleToUnixSocket =
        int (*)(const AHardwareBuffer* buffer, int socketFd);
using PFAHardwareBuffer_unlock = int (*)(AHardwareBuffer* buffer,
                                         int32_t* fence);

using PFeglGetNativeClientBufferANDROID = EGLClientBuffer (*)(const struct AHardwareBuffer *buffer);
#ifdef __cplusplus
}
#endif

struct AHardwareBufferFunctionTable {
public:
    ~AHardwareBufferFunctionTable();

    static std::unique_ptr<AHardwareBufferFunctionTable> create();

    PFAHardwareBuffer_allocate AHardwareBuffer_allocate = nullptr;
    PFAHardwareBuffer_acquire AHardwareBuffer_acquire = nullptr;
    PFAHardwareBuffer_release AHardwareBuffer_release = nullptr;
    PFAHardwareBuffer_lock AHardwareBuffer_lock = nullptr;
    PFAHardwareBuffer_unlock AHardwareBuffer_unlock = nullptr;
    PFAHardwareBuffer_describe AHardwareBuffer_describe = nullptr;
    PFeglGetNativeClientBufferANDROID eglGetNativeClientBufferANDROID = nullptr;

private:
    void *dll_handle = nullptr;
};

