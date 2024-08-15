/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <Metal/Metal.h>
#include <igl/CommandBuffer.h>

namespace igl::metal {

class Device;

class CommandBuffer final : public ICommandBuffer,
                            public std::enable_shared_from_this<CommandBuffer> {
 public:
  CommandBuffer(igl::metal::Device& device, id<MTLCommandBuffer> value);
  ~CommandBuffer() override = default;

  std::unique_ptr<IComputeCommandEncoder> createComputeCommandEncoder() override;

  std::unique_ptr<IRenderCommandEncoder> createRenderCommandEncoder(
      const RenderPassDesc& renderPass,
      const std::shared_ptr<IFramebuffer>& framebuffer,
      const Dependencies& dependencies,
      Result* outResult) override;

  void present(const std::shared_ptr<ITexture>& surface) const override;

  void pushDebugGroupLabel(const char* label, const igl::Color& color) const override;

  void popDebugGroupLabel() const override;

  void waitUntilScheduled() override;

  void waitUntilCompleted() override;

  IGL_INLINE id<MTLCommandBuffer> get() const {
    return value_;
  }
                                
  //@tencent only
  void * getImpl() override { return (__bridge void *)value_;}

  igl::metal::Device& device() {
    return device_;
  }

 private:
  igl::metal::Device& device_;
  id<MTLCommandBuffer> value_;
};

} // namespace igl::metal
