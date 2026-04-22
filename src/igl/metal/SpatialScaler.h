/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#ifndef NOT_USE_UPSCALER

#include <igl/SpatialScaler.h>

#ifdef __OBJC__
#import <Metal/Metal.h>
// Check if MetalFX header is available at compile time
#if __has_include(<MetalFX/MetalFX.h>)
#define IGL_METALFX_AVAILABLE 1
#else
#define IGL_METALFX_AVAILABLE 0
#endif
#endif

namespace igl::metal {

class Device;

/**
 * @brief Metal implementation of ISpatialScaler using MetalFX.
 *
 * This class uses Apple's MetalFX framework for hardware-accelerated spatial upscaling.
 * It requires:
 * - iOS 16.0+ / macOS 13.0+ / tvOS 16.0+
 * - A14 Bionic or newer (iPhone 12+) / M1 or newer (Mac)
 *
 * The scaler leverages the GPU's dedicated hardware for efficient image upscaling
 * with minimal quality loss.
 */
class SpatialScaler final : public ISpatialScaler {
 public:
  /**
   * @brief Checks if MetalFX spatial scaling is supported on the given device.
   *
   * @param device The Metal device to check.
   * @return true if MetalFX spatial scaling is supported.
   */
#ifdef __OBJC__
  static bool isSupported(id<MTLDevice> IGL_NONNULL device);
#endif

  /**
   * @brief Creates a new Metal spatial scaler.
   *
   * @param mtlDevice The Metal device to create the scaler with.
   * @param desc The scaler descriptor.
   * @param outResult Pointer to receive the result of the operation.
   */
#ifdef __OBJC__
  SpatialScaler(id<MTLDevice> IGL_NONNULL mtlDevice,
                const SpatialScalerDesc& desc,
                Result* IGL_NULLABLE outResult);
#endif

  ~SpatialScaler() override;

  // ISpatialScaler interface implementation
  Result setInputTexture(const std::shared_ptr<ITexture>& texture) override;
  Result setOutputTexture(const std::shared_ptr<ITexture>& texture) override;
  Result encode(ICommandBuffer& commandBuffer) override;

  [[nodiscard]] std::shared_ptr<ITexture> getInputTexture() const override;
  [[nodiscard]] std::shared_ptr<ITexture> getOutputTexture() const override;
  [[nodiscard]] const SpatialScalerDesc& getDesc() const override;
  [[nodiscard]] bool isValid() const override;

 private:
  SpatialScalerDesc desc_;

  // Retained MTLDevice for creating internal textures
  void* mtlDevice_ = nullptr;  // id<MTLDevice>
    
#if IGL_METALFX_AVAILABLE
  // Use void* to avoid API availability issues in header
  // Cast to id<MTLFXSpatialScaler> in .mm file at runtime
  void* scaler_ = nullptr;

  // Internal output texture with ShaderWrite usage for MetalFX
  // MetalFX requires output texture to have MTLTextureUsageShaderWrite,
  // but engine's FrameBuffer only creates textures with RenderTarget|ShaderRead.
  // We create an internal texture with the correct usage and blit to the external output.
  void* internalOutputTexture_ = nullptr;  // id<MTLTexture>
  uint32_t internalOutputWidth_ = 0;
  uint32_t internalOutputHeight_ = 0;
#endif

  std::shared_ptr<ITexture> inputTexture_;
  std::shared_ptr<ITexture> outputTexture_;
};

} // namespace igl::metal

#endif
