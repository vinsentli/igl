/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <igl/Core.h>

#if defined(IGL_ANDROID_HWBUFFER_SUPPORTED)

#if !defined(AHARDWAREBUFFER_FORMAT_YCbCr_420_SP_VENUS)
#define AHARDWAREBUFFER_FORMAT_YCbCr_420_SP_VENUS 0x7FA30C06
#endif

#if !defined(COLOR_QCOM_FORMATYUV420PackedSemiPlanar32m)
#define COLOR_QCOM_FORMATYUV420PackedSemiPlanar32m 0x7FA30C04
#endif

#include "AHardwareBufferFunctionTable.h"
#include <android/hardware_buffer.h>
#include <igl/Texture.h>
#include <igl/TextureFormat.h>

namespace igl::android {

class INativeHWTextureBuffer {
 public:
  INativeHWTextureBuffer(AHardwareBufferFunctionTable* funcTable) : funcTable_(funcTable) {}

  struct LockGuard {
   public:
    ~LockGuard();

    LockGuard(const LockGuard&) = delete;
    LockGuard(LockGuard&& g);

   private:
    friend class INativeHWTextureBuffer;

    LockGuard(const INativeHWTextureBuffer* IGL_NONNULL hwBufferOwner = nullptr);

    const INativeHWTextureBuffer* IGL_NULLABLE hwBufferOwner_;
  };

  struct RangeDesc : TextureRangeDesc {
    size_t stride = 0;
  };

  virtual ~INativeHWTextureBuffer() {
    if (hwBuffer_ != nullptr) {
      funcTable_->AHardwareBuffer_release(hwBuffer_);
      hwBuffer_ = nullptr;
    }
  }

  Result createWithHWBuffer(AHardwareBuffer* IGL_NULLABLE buffer);

  Result createHWBuffer(const TextureDesc& desc, bool hasStorageAlready, bool surfaceComposite);

  [[nodiscard]] LockGuard lockHWBuffer(std::byte* IGL_NULLABLE* IGL_NONNULL dst,
                                       RangeDesc& outRange,
                                       Result* IGL_NULLABLE outResult) const;

  Result lockHWBuffer(std::byte* IGL_NULLABLE* IGL_NONNULL dst, RangeDesc& outRange) const;
  Result unlockHWBuffer() const;

  [[nodiscard]] AHardwareBuffer* IGL_NULLABLE getHardwareBuffer() const;

  [[nodiscard]] TextureDesc getTextureDesc() const;

 protected:
  virtual Result createTextureInternal(AHardwareBuffer* IGL_NULLABLE buffer) = 0;
  AHardwareBuffer* IGL_NULLABLE hwBuffer_ = nullptr;
  TextureDesc textureDesc_;
  AHardwareBufferFunctionTable* funcTable_ = nullptr;
};

// utils

uint32_t getNativeHWFormat(TextureFormat iglFormat);
uint32_t getNativeHWBufferUsage(TextureDesc::TextureUsage iglUsage);

TextureFormat getIglFormat(uint32_t nativeFormat);
TextureDesc::TextureUsage getIglBufferUsage(uint32_t nativeUsage);

Result allocateNativeHWBuffer(AHardwareBufferFunctionTable* funcTable,
                              const TextureDesc& desc,
                              bool surfaceComposite,
                              AHardwareBuffer* IGL_NULLABLE* IGL_NONNULL buffer);

} // namespace igl::android

#endif // defined(IGL_ANDROID_HWBUFFER_SUPPORTED)
