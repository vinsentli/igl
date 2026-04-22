//
// Created by vinsentli@tencent.com on 2025/3/10.
//
#include "AHardwareBufferFunctionTable.h"
#include <android/api-level.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <igl/Common.h>

AHardwareBufferFunctionTable::~AHardwareBufferFunctionTable() {
  if (dll_handle) {
    dlclose(dll_handle);
  }
}

std::unique_ptr<AHardwareBufferFunctionTable> AHardwareBufferFunctionTable::create() {
  void* dll_handle = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
  if (!dll_handle) {
    IGL_DEBUG_ASSERT(false);
    return nullptr;
  }

  std::unique_ptr<AHardwareBufferFunctionTable> functionTable =
      std::make_unique<AHardwareBufferFunctionTable>();

  functionTable->dll_handle = dll_handle;

  functionTable->AHardwareBuffer_allocate =
      (PFAHardwareBuffer_allocate)dlsym(dll_handle, "AHardwareBuffer_allocate");
  functionTable->AHardwareBuffer_acquire =
      (PFAHardwareBuffer_acquire)dlsym(dll_handle, "AHardwareBuffer_acquire");
  functionTable->AHardwareBuffer_release =
      (PFAHardwareBuffer_release)dlsym(dll_handle, "AHardwareBuffer_release");
  functionTable->AHardwareBuffer_lock =
      (PFAHardwareBuffer_lock)dlsym(dll_handle, "AHardwareBuffer_lock");
  functionTable->AHardwareBuffer_unlock =
      (PFAHardwareBuffer_unlock)dlsym(dll_handle, "AHardwareBuffer_unlock");
  functionTable->AHardwareBuffer_describe =
      (PFAHardwareBuffer_describe)dlsym(dll_handle, "AHardwareBuffer_describe");

  //优先通过eglGetProcAddress获取eglGetNativeClientBufferANDROID接口地址
  functionTable->eglGetNativeClientBufferANDROID = reinterpret_cast<PFeglGetNativeClientBufferANDROID>
          (eglGetProcAddress("eglGetNativeClientBufferANDROID"));

  if (!functionTable->eglGetNativeClientBufferANDROID) {
      functionTable->eglGetNativeClientBufferANDROID =
              (PFeglGetNativeClientBufferANDROID) dlsym(dll_handle,
                                                        "eglGetNativeClientBufferANDROID");
  }

  functionTable->ASurfaceControl_createFromWindow =
          (PFASurfaceControl_createFromWindow)dlsym(dll_handle, "ASurfaceControl_createFromWindow");
  functionTable->ASurfaceControl_release =
          (PFASurfaceControl_release)dlsym(dll_handle, "ASurfaceControl_release");
  functionTable->ASurfaceTransaction_create =
          (PFASurfaceTransaction_create)dlsym(dll_handle, "ASurfaceTransaction_create");
  functionTable->ASurfaceTransaction_delete =
          (PFASurfaceTransaction_delete)dlsym(dll_handle, "ASurfaceTransaction_delete");
  functionTable->ASurfaceTransaction_apply =
          (PFASurfaceTransaction_apply)dlsym(dll_handle, "ASurfaceTransaction_apply");
  functionTable->ASurfaceTransaction_setBuffer =
          (PFASurfaceTransaction_setBuffer)dlsym(dll_handle, "ASurfaceTransaction_setBuffer");
  functionTable->ASurfaceTransaction_setOnComplete =
          (PFASurfaceTransaction_setOnComplete)dlsym(dll_handle, "ASurfaceTransaction_setOnComplete");

  if(!functionTable->AHardwareBuffer_allocate) return nullptr;
  if(!functionTable->AHardwareBuffer_acquire) return nullptr;
  if(!functionTable->AHardwareBuffer_release) return nullptr;
  if(!functionTable->AHardwareBuffer_lock) return nullptr;
  if(!functionTable->AHardwareBuffer_unlock) return nullptr;
  if(!functionTable->AHardwareBuffer_describe) return nullptr;
  if(!functionTable->eglGetNativeClientBufferANDROID) return nullptr;

  if(!functionTable->ASurfaceControl_createFromWindow) return nullptr;
  if(!functionTable->ASurfaceControl_release) return nullptr;
  if(!functionTable->ASurfaceTransaction_create) return nullptr;
  if(!functionTable->ASurfaceTransaction_delete) return nullptr;
  if(!functionTable->ASurfaceTransaction_apply) return nullptr;
  if(!functionTable->ASurfaceTransaction_setBuffer) return nullptr;
  if(!functionTable->ASurfaceTransaction_setOnComplete) return nullptr;

  return functionTable;
}
