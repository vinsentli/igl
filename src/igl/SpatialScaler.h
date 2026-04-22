/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#ifndef NOT_USE_UPSCALER

#include <igl/Common.h>
#include <igl/ITrackedResource.h>
#include <igl/Texture.h>

namespace igl {

class ICommandBuffer;

/**
 * @brief Describes the configuration for a spatial scaler.
 *
 * This describes the input/output dimensions, texture format, and quality settings
 * for spatial upscaling operations. Spatial scaling uses hardware-accelerated algorithms
 * (e.g., MetalFX on Apple devices) to upscale images with minimal artifacts.
 */
struct SpatialScalerDesc {
  /**
   * @brief The width of the input texture in pixels.
   */
  uint32_t inputWidth = 0;

  /**
   * @brief The height of the input texture in pixels.
   */
  uint32_t inputHeight = 0;

  /**
   * @brief The width of the output texture in pixels.
   */
  uint32_t outputWidth = 0;

  /**
   * @brief The height of the output texture in pixels.
   */
  uint32_t outputHeight = 0;

  /**
   * @brief The texture format for both input and output textures.
   *
   * The input and output textures must use the same format.
   */
  TextureFormat colorFormat = TextureFormat::BGRA_UNorm8;

  /**
   * @brief A user-readable debug name associated with this scaler.
   */
  std::string debugName;

  /**
   * @brief Compares two SpatialScalerDesc objects for equality.
   */
  bool operator==(const SpatialScalerDesc& rhs) const {
    return inputWidth == rhs.inputWidth && inputHeight == rhs.inputHeight &&
           outputWidth == rhs.outputWidth && outputHeight == rhs.outputHeight &&
           colorFormat == rhs.colorFormat;
  }

  /**
   * @brief Compares two SpatialScalerDesc objects for inequality.
   */
  bool operator!=(const SpatialScalerDesc& rhs) const {
    return !(*this == rhs);
  }

  /**
   * @brief Validates the descriptor.
   *
   * Returns true if input dimensions are less than output dimensions
   * and all dimensions are greater than zero.
   */
  [[nodiscard]] bool isValid() const {
    return inputWidth > 0 && inputHeight > 0 && outputWidth > 0 && outputHeight > 0 &&
           inputWidth <= outputWidth && inputHeight <= outputHeight;
  }

  /**
   * @brief Calculates the horizontal scale factor.
   */
  [[nodiscard]] float getScaleX() const {
    return inputWidth > 0 ? static_cast<float>(outputWidth) / static_cast<float>(inputWidth) : 1.0f;
  }

  /**
   * @brief Calculates the vertical scale factor.
   */
  [[nodiscard]] float getScaleY() const {
    return inputHeight > 0 ? static_cast<float>(outputHeight) / static_cast<float>(inputHeight)
                           : 1.0f;
  }
};

/**
 * @brief A hardware-accelerated spatial scaler interface.
 *
 * This interface is the backend-agnostic representation for spatial scaling operations.
 * To create an instance, populate a SpatialScalerDesc and call IDevice.createSpatialScaler.
 *
 * Spatial scalers use hardware-accelerated algorithms to upscale images:
 * - On Apple devices (iOS 16+/macOS 13+): Uses MetalFX Spatial Scaler
 * - On other platforms: Returns nullptr (engine should fall back to shader-based solutions)
 *
 * Usage:
 * 1. Create scaler with IDevice::createSpatialScaler()
 * 2. Set input/output textures with setInputTexture()/setOutputTexture()
 * 3. Call encode() to submit scaling commands to command buffer
 */
class ISpatialScaler : public ITrackedResource<ISpatialScaler> {
 protected:
  ISpatialScaler() = default;

 public:
  virtual ~ISpatialScaler() = default;

  /**
   * @brief Sets the input texture for the scaling operation.
   *
   * @param texture The input texture to be upscaled. Must match the inputWidth/inputHeight
   *                from the descriptor.
   * @return Result indicating success or failure.
   */
  virtual Result setInputTexture(const std::shared_ptr<ITexture>& texture) = 0;

  /**
   * @brief Sets the output texture for the scaling operation.
   *
   * @param texture The output texture to receive the upscaled result. Must match the
   *                outputWidth/outputHeight from the descriptor.
   * @return Result indicating success or failure.
   */
  virtual Result setOutputTexture(const std::shared_ptr<ITexture>& texture) = 0;

  /**
   * @brief Encodes the spatial scaling operation to a command buffer.
   *
   * The input and output textures must be set before calling this method.
   *
   * @param commandBuffer The command buffer to encode the scaling operation to.
   * @return Result indicating success or failure.
   */
  virtual Result encode(ICommandBuffer& commandBuffer) = 0;

  /**
   * @brief Gets the currently set input texture.
   *
   * @return Shared pointer to the input texture, or nullptr if not set.
   */
  [[nodiscard]] virtual std::shared_ptr<ITexture> getInputTexture() const = 0;

  /**
   * @brief Gets the currently set output texture.
   *
   * @return Shared pointer to the output texture, or nullptr if not set.
   */
  [[nodiscard]] virtual std::shared_ptr<ITexture> getOutputTexture() const = 0;

  /**
   * @brief Gets the descriptor used to create this scaler.
   *
   * @return Reference to the descriptor.
   */
  [[nodiscard]] virtual const SpatialScalerDesc& getDesc() const = 0;

  /**
   * @brief Checks if this scaler is valid and ready to use.
   *
   * @return true if the scaler was created successfully and is ready for use.
   */
  [[nodiscard]] virtual bool isValid() const = 0;
};

} // namespace igl

#endif
