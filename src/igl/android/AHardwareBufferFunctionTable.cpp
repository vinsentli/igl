//
// Created by vinsentli@tencent.com on 2025/3/10.
//
#include "AHardwareBufferFunctionTable.h"
#include <android/hardware_buffer.h>
#include <android/api-level.h>
#include <igl/Common.h>
#include <dlfcn.h>

AHardwareBufferFunctionTable::~AHardwareBufferFunctionTable(){
    if (dll_handle){
        dlclose(dll_handle);
    }
}

std::unique_ptr<AHardwareBufferFunctionTable> AHardwareBufferFunctionTable::create() {
    //AHardwareBuffer需要 Android 8.0（API 26）或更高版本。
    if (android_get_device_api_level() < 26) {
        return nullptr;
    }

    void *dll_handle = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!dll_handle){
        IGL_DEBUG_ASSERT(false);
        return nullptr;
    }

    std::unique_ptr<AHardwareBufferFunctionTable> functionTable = std::make_unique<AHardwareBufferFunctionTable>();

    functionTable->dll_handle = dll_handle;

    functionTable->AHardwareBuffer_allocate = (PFAHardwareBuffer_allocate)dlsym(dll_handle, "AHardwareBuffer_allocate");
    functionTable->AHardwareBuffer_acquire = (PFAHardwareBuffer_acquire)dlsym(dll_handle, "AHardwareBuffer_acquire");
    functionTable->AHardwareBuffer_release = (PFAHardwareBuffer_release)dlsym(dll_handle, "AHardwareBuffer_release");
    functionTable->AHardwareBuffer_lock = (PFAHardwareBuffer_lock)dlsym(dll_handle, "AHardwareBuffer_lock");
    functionTable->AHardwareBuffer_unlock = (PFAHardwareBuffer_unlock)dlsym(dll_handle, "AHardwareBuffer_unlock");
    functionTable->AHardwareBuffer_describe = (PFAHardwareBuffer_describe)dlsym(dll_handle, "AHardwareBuffer_describe");
    functionTable->eglGetNativeClientBufferANDROID = (PFeglGetNativeClientBufferANDROID)dlsym(dll_handle, "eglGetNativeClientBufferANDROID");

    IGL_DEBUG_ASSERT(functionTable->AHardwareBuffer_allocate);
    IGL_DEBUG_ASSERT(functionTable->AHardwareBuffer_acquire);
    IGL_DEBUG_ASSERT(functionTable->AHardwareBuffer_release);
    IGL_DEBUG_ASSERT(functionTable->AHardwareBuffer_lock);
    IGL_DEBUG_ASSERT(functionTable->AHardwareBuffer_unlock);
    IGL_DEBUG_ASSERT(functionTable->AHardwareBuffer_describe);
    IGL_DEBUG_ASSERT(functionTable->eglGetNativeClientBufferANDROID);

    return functionTable;
}
