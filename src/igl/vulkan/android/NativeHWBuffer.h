/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// @fb-only
// @fb-only

#pragma once

#include <igl/android/NativeHWBuffer.h>

#if defined(IGL_ANDROID_HWBUFFER_SUPPORTED)

#include <igl/vulkan/Texture.h>

struct AHardwareBuffer;

namespace igl::vulkan::android {

typedef void AHardwareBufferHelper;

// TextureBuffer encapsulates Vulkan textures
class NativeHWTextureBuffer : public igl::android::INativeHWTextureBuffer, public Texture {
  friend class igl::vulkan::PlatformDevice;
  using Super = Texture;

 public:
  NativeHWTextureBuffer(igl::vulkan::Device& device, std::shared_ptr<AHardwareBufferFunctionTable> funcTable, TextureFormat format);
  ~NativeHWTextureBuffer() override;

  void* getMapMemoryAddress() const override;
  size_t getMapBytesPerRow() const override;

 protected:
  // Texture overrides
  Result create(const TextureDesc& desc) override;

  Result createTextureInternal(AHardwareBuffer* buffer) override;

  Result uploadInternal(TextureType type,
                        const TextureRangeDesc& range,
                        const void* IGL_NULLABLE data,
                        size_t bytesPerRow,
                        const uint32_t* IGL_NULLABLE mipLevelBytes) const override;
};

} // namespace igl::vulkan::android

#endif // defined(IGL_ANDROID_HWBUFFER_SUPPORTED)
