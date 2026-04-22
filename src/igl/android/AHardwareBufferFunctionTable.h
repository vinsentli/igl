//
// Created by vinsentli@tencent.com on 2025/3/10.
//

#pragma once
#include <EGL/egl.h>
#include <memory>

struct ARect;
struct AHardwareBuffer;
struct AHardwareBuffer_Desc;
struct ASurfaceControl;
struct ASurfaceTransaction;
struct ASurfaceTransactionStats;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnullability-completeness"

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
using PFAHardwareBuffer_recvHandleFromUnixSocket = int (*)(int socketFd,
                                                           AHardwareBuffer** outBuffer);
using PFAHardwareBuffer_release = void (*)(AHardwareBuffer* buffer);
using PFAHardwareBuffer_sendHandleToUnixSocket = int (*)(const AHardwareBuffer* buffer,
                                                         int socketFd);
using PFAHardwareBuffer_unlock = int (*)(AHardwareBuffer* buffer, int32_t* fence);

using PFeglGetNativeClientBufferANDROID = EGLClientBuffer (*)(const struct AHardwareBuffer* buffer);

using PFASurfaceControl_createFromWindow = ASurfaceControl* (*)(ANativeWindow *,const char *);
using PFASurfaceControl_release = void (*)(ASurfaceControl*);
using PFASurfaceTransaction_create = ASurfaceTransaction* (*)();
using PFASurfaceTransaction_delete = void (*)(ASurfaceTransaction*);
using PFASurfaceTransaction_apply = void (*)(ASurfaceTransaction*);
using PFASurfaceTransaction_setBuffer = void (*)(ASurfaceTransaction *, ASurfaceControl *, AHardwareBuffer *, int);
typedef void(* ASurfaceTransaction_OnComplete_Callback)(void *_Null_unspecified context, ASurfaceTransactionStats *_Nonnull stats);
using PFASurfaceTransaction_setOnComplete = void (*)(
  ASurfaceTransaction *_Nonnull transaction,
  void *_Null_unspecified context,
  ASurfaceTransaction_OnComplete_Callback _Nonnull func
);

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

  PFASurfaceControl_createFromWindow ASurfaceControl_createFromWindow = nullptr;
  PFASurfaceControl_release ASurfaceControl_release = nullptr;
  PFASurfaceTransaction_create ASurfaceTransaction_create = nullptr;
  PFASurfaceTransaction_delete ASurfaceTransaction_delete = nullptr;
  PFASurfaceTransaction_apply ASurfaceTransaction_apply = nullptr;
  PFASurfaceTransaction_setBuffer ASurfaceTransaction_setBuffer = nullptr;
  PFASurfaceTransaction_setOnComplete ASurfaceTransaction_setOnComplete = nullptr;

 private:
  void* dll_handle = nullptr;
};

#pragma GCC diagnostic pop
