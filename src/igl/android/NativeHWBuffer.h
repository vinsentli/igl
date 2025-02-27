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

#include <igl/Texture.h>
#include <igl/TextureFormat.h>

struct AHardwareBuffer;

namespace igl::android {

class INativeHWTextureBuffer {
 public:
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

  virtual ~INativeHWTextureBuffer();

  Result attachHWBuffer(AHardwareBuffer* IGL_NULLABLE buffer);
  Result createHWBuffer(const TextureDesc& desc, bool hasStorageAlready, bool surfaceComposite);

  [[nodiscard]] LockGuard lockHWBuffer(std::byte* IGL_NULLABLE* IGL_NONNULL dst,
                                       RangeDesc& outRange,
                                       Result* IGL_NULLABLE outResult) const;

  Result lockHWBuffer(std::byte* IGL_NULLABLE* IGL_NONNULL dst, RangeDesc& outRange) const;
  Result unlockHWBuffer() const;

  [[nodiscard]] AHardwareBuffer* IGL_NULLABLE getHardwareBuffer() const;

  [[nodiscard]] TextureDesc getTextureDesc() const;

 protected:
  virtual Result createTextureInternal(const TextureDesc& desc, AHardwareBuffer* IGL_NULLABLE buffer) = 0;

  AHardwareBuffer* IGL_NULLABLE hwBuffer_ = nullptr;
  TextureDesc textureDesc_;
};

// utils

uint32_t getNativeHWFormat(TextureFormat iglFormat);
uint32_t getNativeHWBufferUsage(TextureDesc::TextureUsage iglUsage);

TextureFormat getIglFormat(uint32_t nativeFormat);
TextureDesc::TextureUsage getIglBufferUsage(uint32_t nativeUsage);

Result allocateNativeHWBuffer(const TextureDesc& desc,
                              bool surfaceComposite,
                              AHardwareBuffer* IGL_NULLABLE * IGL_NONNULL buffer);

} // namespace igl::android

#endif // defined(IGL_ANDROID_HWBUFFER_SUPPORTED)
