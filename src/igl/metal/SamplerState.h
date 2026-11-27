/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <Metal/MTLSampler.h>
#include <igl/SamplerState.h>

namespace igl::metal {

class BindlessResources;

class SamplerState final : public ISamplerState {
 public:
  explicit SamplerState(id<MTLSamplerState> value);
  ~SamplerState() override;
  IGL_INLINE id<MTLSamplerState> get() const {
    return value_;
  }

  void setBindlessSamplerId(BindlessResources* resources, uint32_t samplerId) {
    bindlessResources_ = resources;
    samplerId_ = samplerId;
  }

  static MTLSamplerMinMagFilter convertMinMagFilter(SamplerMinMagFilter value);
  static MTLSamplerMipFilter convertMipFilter(SamplerMipFilter value);
  static MTLSamplerAddressMode convertAddressMode(SamplerAddressMode value);

  /**
   * @brief Returns true if this sampler is a YUV sampler.
   */
  [[nodiscard]] bool isYUV() const noexcept override;

  [[nodiscard]] uint32_t getSamplerId() const override {
    return samplerId_;
  }

 private:
  id<MTLSamplerState> value_;
  BindlessResources* bindlessResources_ = nullptr;
  uint32_t samplerId_ = 0;
};

} // namespace igl::metal
