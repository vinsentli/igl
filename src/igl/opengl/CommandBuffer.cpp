/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <igl/opengl/CommandBuffer.h>

#include <igl/Macros.h>
#include <igl/opengl/Buffer.h>
#include <igl/opengl/ComputeCommandEncoder.h>
#include <igl/opengl/IContext.h>
#include <igl/opengl/RenderCommandEncoder.h>

namespace igl::opengl {

CommandBuffer::CommandBuffer(std::shared_ptr<IContext> context, CommandBufferDesc desc) :
  ICommandBuffer(std::move(desc)), context_(std::move(context)) {}

CommandBuffer::~CommandBuffer() = default;

std::unique_ptr<IRenderCommandEncoder> CommandBuffer::createRenderCommandEncoder(
    const RenderPassDesc& renderPass,
    const std::shared_ptr<IFramebuffer>& framebuffer,
    const Dependencies& dependencies,
    Result* outResult) {
  IGL_PROFILER_FUNCTION_COLOR(IGL_PROFILER_COLOR_CREATE);
  return RenderCommandEncoder::create(
      shared_from_this(), renderPass, framebuffer, dependencies, outResult);
}

std::unique_ptr<IComputeCommandEncoder> CommandBuffer::createComputeCommandEncoder() {
  IGL_PROFILER_FUNCTION_COLOR(IGL_PROFILER_COLOR_CREATE);
  return std::make_unique<ComputeCommandEncoder>(shared_from_this()->getContext());
}

void CommandBuffer::present(const std::shared_ptr<ITexture>& surface) const {
  IGL_PROFILER_FUNCTION_COLOR(IGL_PROFILER_COLOR_PRESENT);
  context_->present(surface);
}

void CommandBuffer::waitUntilScheduled() {
  IGL_PROFILER_FUNCTION_COLOR(IGL_PROFILER_COLOR_WAIT);
  context_->flush();
}

void CommandBuffer::waitUntilCompleted() {
  IGL_PROFILER_FUNCTION_COLOR(IGL_PROFILER_COLOR_WAIT);
  context_->finish();
}

void CommandBuffer::pushDebugGroupLabel(const char* label, const Color& /*color*/) const {
  IGL_PROFILER_FUNCTION();
  IGL_DEBUG_ASSERT(label != nullptr && *label);
  if (getContext().deviceFeatures().hasInternalFeature(InternalFeatures::DebugMessage)) {
    getContext().pushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, label);
  } else {
    IGL_LOG_ERROR_ONCE("CommandBuffer::pushDebugGroupLabel not supported in this context!\n");
  }
}

void CommandBuffer::popDebugGroupLabel() const {
  IGL_PROFILER_FUNCTION();
  if (getContext().deviceFeatures().hasInternalFeature(InternalFeatures::DebugMessage)) {
    getContext().popDebugGroup();
  } else {
    IGL_LOG_ERROR_ONCE("CommandBuffer::popDebugGroupLabel not supported in this context!\n");
  }
}

void CommandBuffer::copyBuffer(IBuffer& src,
                               IBuffer& dst,
                               uint64_t srcOffset,
                               uint64_t dstOffset,
                               uint64_t size) {
  IGL_PROFILER_FUNCTION();
  IContext& ctx = getContext();

  if (!ctx.deviceFeatures().hasFeature(igl::DeviceFeatures::CopyBuffer)) {
    IGL_LOG_ERROR_ONCE("CommandBuffer::copyBuffer() not supported in this context!\n");
    return;
  }

  auto& srcBuffer = static_cast<ArrayBuffer&>(src);
  auto& dstBuffer = static_cast<ArrayBuffer&>(dst);

  ctx.bindBuffer(GL_COPY_READ_BUFFER, srcBuffer.getId());
  ctx.bindBuffer(GL_COPY_WRITE_BUFFER, dstBuffer.getId());
  ctx.copyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, srcOffset, dstOffset, size);
  ctx.bindBuffer(GL_COPY_READ_BUFFER, 0);
  ctx.bindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void CommandBuffer::copyTextureToBuffer(ITexture& src,
                                        IBuffer& dst,
                                        uint64_t dstOffset,
                                        uint32_t level,
                                        uint32_t layer,
                                        ImageAspectFlags aspect) {
  IGL_PROFILER_FUNCTION();
  IContext& ctx = getContext();

  // glReadPixels cannot read depth/stencil attachments on GLES
  // (copyBytesDepthAttachment/copyBytesStencilAttachment are unimplemented for the same reason).
  if (src.getProperties().isDepthOrStencil()) {
    IGL_LOG_ERROR_ONCE(
        "CommandBuffer::copyTextureToBuffer: depth/stencil formats are not supported on the "
        "OpenGL backend\n");
    IGL_DEBUG_ASSERT(false, "depth/stencil formats are not supported on the OpenGL backend");
    return;
  }
  IGL_DEBUG_ASSERT(aspect == ImageAspectBits_Invalid || aspect == ImageAspectBits_Color,
                   "invalid aspect for color texture");

  IGL_DEBUG_ASSERT(src.getSamples() == 1, "copyTextureToBuffer does not support MSAA textures");
  IGL_DEBUG_ASSERT(level < src.getNumMipLevels(), "level is out of range");

  auto& texture = static_cast<Texture&>(src);
  auto& dstBuffer = static_cast<ArrayBuffer&>(dst);

  const auto dimensions = src.getDimensions();
  const auto levelWidth = static_cast<GLsizei>(std::max(dimensions.width >> level, 1u));
  const auto levelHeight = static_cast<GLsizei>(std::max(dimensions.height >> level, 1u));

  const FramebufferBindingGuard guard(ctx);

  // Non-owning aliasing shared_ptr: only referenced within this scope.
  std::shared_ptr<ITexture> srcAlias(&src, [](ITexture*) {});

  CustomFramebuffer extraFramebuffer(ctx);
  Result ret;
  const FramebufferDesc desc{
      .colorAttachments = {{.texture = srcAlias}},
  };
  extraFramebuffer.initialize(desc, &ret);
  if (!IGL_DEBUG_VERIFY(ret.isOk(), ret.message.c_str())) {
    return;
  }
  extraFramebuffer.bindBufferForRead();

  // Reattach the exact (level, layer/face) subresource for reading.
  Texture::AttachmentParams params{};
  params.face = src.getType() == TextureType::Cube ? layer : 0;
  params.mipLevel = level;
  params.layer = src.getType() == TextureType::Cube ? 0 : layer;
  params.read = true;
  params.stereo = false;
  texture.attachAsColor(0, params);

  // Tightly packed rows: PACK_ALIGNMENT 1 removes the PBO offset/row alignment constraint.
  ctx.pixelStorei(GL_PACK_ALIGNMENT, 1);

  ctx.bindBuffer(GL_PIXEL_PACK_BUFFER, dstBuffer.getId());
  ctx.flush();

  // NOTE: glReadPixels origin is bottom-left, so the row order is vertically flipped
  // compared to the Metal/Vulkan implementations. Callers needing pixel-identical results
  // across backends must handle the flip.
  const auto halfFloatType =
      ctx.deviceFeatures().hasInternalRequirement(InternalRequirement::TextureHalfFloatExtReq)
          ? GL_HALF_FLOAT_OES
          : GL_HALF_FLOAT;
  readPixelsByFormat(ctx,
                     src.getFormat(),
                     0,
                     0,
                     levelWidth,
                     levelHeight,
                     reinterpret_cast<void*>(static_cast<uintptr_t>(dstOffset)),
                     halfFloatType);

  ctx.bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  ctx.pixelStorei(GL_PACK_ALIGNMENT, 4);

  const auto error = ctx.getLastError();
  IGL_DEBUG_ASSERT(error.isOk(), error.message.c_str());
}

IContext& CommandBuffer::getContext() const {
  return *context_;
}

} // namespace igl::opengl
