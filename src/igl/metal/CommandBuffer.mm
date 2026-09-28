/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <igl/metal/CommandBuffer.h>

#import <Foundation/NSRange.h>
#import <Foundation/NSString.h>
#import <Metal/Metal.h>
#include <igl/Macros.h>
#include <igl/metal/Buffer.h>
#include <igl/metal/ComputeCommandEncoder.h>
#include <igl/metal/RenderCommandEncoder.h>
#include <igl/metal/Texture.h>
namespace igl::metal {

CommandBuffer::CommandBuffer(Device& device, id<MTLCommandBuffer> value, CommandBufferDesc desc) :
  ICommandBuffer(std::move(desc)), device_(device), value_(value) {}

std::unique_ptr<IComputeCommandEncoder> CommandBuffer::createComputeCommandEncoder() {
  IGL_PROFILER_FUNCTION_COLOR(IGL_PROFILER_COLOR_CREATE);
  return std::make_unique<ComputeCommandEncoder>(value_);
}

std::unique_ptr<IComputeCommandEncoder> CommandBuffer::createComputeCommandEncoder(
    const ComputePassDesc& computePass) {
  IGL_PROFILER_FUNCTION_COLOR(IGL_PROFILER_COLOR_CREATE);
  return std::make_unique<ComputeCommandEncoder>(value_, computePass);
}

std::unique_ptr<IRenderCommandEncoder> CommandBuffer::createRenderCommandEncoder(
    const RenderPassDesc& renderPass,
    const std::shared_ptr<IFramebuffer>& framebuffer,
    const Dependencies& /*dependencies*/,
    Result* outResult) {
  IGL_PROFILER_FUNCTION_COLOR(IGL_PROFILER_COLOR_CREATE);
  return RenderCommandEncoder::create(shared_from_this(), renderPass, framebuffer, outResult);
}

void CommandBuffer::present(const std::shared_ptr<ITexture>& surface) const {
  IGL_PROFILER_FUNCTION_COLOR(IGL_PROFILER_COLOR_PRESENT);
  IGL_DEBUG_ASSERT(surface);
  if (!surface) {
    return;
  }
  const auto drawable = static_cast<Texture&>(*surface).getDrawable();
  if (drawable != nullptr) {
    [value_ presentDrawable:drawable];
  }
}

void CommandBuffer::pushDebugGroupLabel(const char* label, const Color& /*color*/) const {
  IGL_PROFILER_FUNCTION();
  IGL_DEBUG_ASSERT(label != nullptr && *label);
  [value_ pushDebugGroup:[NSString stringWithUTF8String:label] ?: @""];
}

void CommandBuffer::popDebugGroupLabel() const {
  [value_ popDebugGroup];
}

void CommandBuffer::copyBuffer(IBuffer& src,
                               IBuffer& dst,
                               uint64_t srcOffset,
                               uint64_t dstOffset,
                               uint64_t size) {
  IGL_PROFILER_FUNCTION();
  auto srcBuffer = static_cast<Buffer&>(src).get();
  auto dstBuffer = static_cast<Buffer&>(dst).get();

  auto blitCommandEncoder = [value_ blitCommandEncoder];
  [blitCommandEncoder copyFromBuffer:srcBuffer
                        sourceOffset:srcOffset
                            toBuffer:dstBuffer
                   destinationOffset:dstOffset
                                size:size];
  [blitCommandEncoder endEncoding];
}

void CommandBuffer::fillBuffer(IBuffer& buffer, const BufferRange& range, uint8_t value) {
  IGL_PROFILER_FUNCTION();
  auto metalBuffer = static_cast<Buffer&>(buffer).get();
  IGL_DEBUG_ASSERT(range.offset % 4u == 0u && range.size % 4u == 0u);
  IGL_DEBUG_ASSERT(range.offset + range.size <= metalBuffer.length);

  auto blitCommandEncoder = [value_ blitCommandEncoder];
  [blitCommandEncoder fillBuffer:metalBuffer
                           range:NSMakeRange(range.offset, range.size)
                           value:value];
  [blitCommandEncoder endEncoding];
}

void CommandBuffer::copyTextureToBuffer(ITexture& src,
                                        IBuffer& dst,
                                        uint64_t dstOffset,
                                        uint32_t level,
                                        uint32_t layer,
                                        ImageAspectFlags aspect) {
  id<MTLTexture> srcTexture = static_cast<Texture&>(src).get();
  id<MTLBuffer> dstBuffer = static_cast<Buffer&>(dst).get();

  if (!srcTexture || !dstBuffer) {
    IGL_DEBUG_ASSERT(false, "copyTextureToBuffer: src texture or dst buffer is nil");
    return;
  }

  // Multisampled textures are not supported by blit copy operations.
  IGL_DEBUG_ASSERT(src.getSamples() == 1, "copyTextureToBuffer does not support MSAA textures");

  // For 2D textures arrayLength is 1; for cube textures it's 6; for 2D arrays it's the layer count.
  IGL_DEBUG_ASSERT(layer < srcTexture.arrayLength, "layer is out of range");
  IGL_DEBUG_ASSERT(level < srcTexture.mipmapLevelCount, "level is out of range");

  // Dimensions of the requested mip level, minimum 1 (e.g. NPOT textures with full mip chain).
  // Note: Metal requires depth/stencil copies to cover the whole subresource anyway,
  // and this function always copies the whole (level, layer) subresource.
  const NSUInteger levelWidth = std::max<NSUInteger>(srcTexture.width >> level, 1);
  const NSUInteger levelHeight = std::max<NSUInteger>(srcTexture.height >> level, 1);

  const auto& props = src.getProperties();
  MTLBlitOption blitOption = MTLBlitOptionNone;

  NSUInteger dstBytesPerRow = 0;
  NSUInteger dstBytesPerImage = 0;

  if (props.isDepthOrStencil()) {
    const bool wantStencil = (aspect == ImageAspectBits_Stencil);
    NSUInteger bytesPerPixel = 0;

    switch (srcTexture.pixelFormat) {
    case MTLPixelFormatDepth32Float_Stencil8:
      // Combined depth-stencil: Metal cannot copy the interleaved data as-is; depth and
      // stencil must be copied in separate calls with a component-specific blit option.
      // Default (Invalid) selects depth, matching the Vulkan backend behavior.
      if (wantStencil) {
        blitOption = MTLBlitOptionStencilFromDepthStencil;
        bytesPerPixel = 1;  // stencil8
      } else {
        IGL_DEBUG_ASSERT(aspect == ImageAspectBits_Invalid || aspect == ImageAspectBits_Depth,
                         "invalid aspect for depth-stencil texture");
        blitOption = MTLBlitOptionDepthFromDepthStencil;
        bytesPerPixel = 4;  // depth32float
      }
      break;
    case MTLPixelFormatX32_Stencil8:
      // Stencil-only with 24 padding bits: copied as-is, no blit option needed.
      IGL_DEBUG_ASSERT(aspect == ImageAspectBits_Invalid || aspect == ImageAspectBits_Stencil,
                       "invalid aspect for stencil-only texture");
      bytesPerPixel = 1;
      break;
    case MTLPixelFormatStencil8:
      IGL_DEBUG_ASSERT(aspect == ImageAspectBits_Invalid || aspect == ImageAspectBits_Stencil,
                       "invalid aspect for stencil-only texture");
      bytesPerPixel = 1;
      break;
    case MTLPixelFormatDepth32Float:
      IGL_DEBUG_ASSERT(aspect == ImageAspectBits_Invalid || aspect == ImageAspectBits_Depth,
                       "invalid aspect for depth-only texture");
      bytesPerPixel = 4;
      break;
#if (defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_13_0) || \
    (defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= __MAC_10_12)
    case MTLPixelFormatDepth16Unorm: 
      IGL_DEBUG_ASSERT(aspect == ImageAspectBits_Invalid || aspect == ImageAspectBits_Depth,
                       "invalid aspect for depth-only texture");
      bytesPerPixel = 2;
      break;
#endif
#if TARGET_OS_OSX
    case MTLPixelFormatDepth24Unorm_Stencil8:
      // macOS-only packed depth24+stencil8; the depth component copies as 32-bit unorm data.
      if (wantStencil) {
        blitOption = MTLBlitOptionStencilFromDepthStencil;
        bytesPerPixel = 1;
      } else {
        IGL_DEBUG_ASSERT(aspect == ImageAspectBits_Invalid || aspect == ImageAspectBits_Depth,
                         "invalid aspect for depth-stencil texture");
        blitOption = MTLBlitOptionDepthFromDepthStencil;
        bytesPerPixel = 4;
      }
      break;
    case MTLPixelFormatX24_Stencil8:
      IGL_DEBUG_ASSERT(aspect == ImageAspectBits_Invalid || aspect == ImageAspectBits_Stencil,
                       "invalid aspect for stencil-only texture");
      bytesPerPixel = 1;
      break;
#endif
    default:
      IGL_DEBUG_ASSERT(false, "copyTextureToBuffer: unsupported depth/stencil pixel format");
      return;
    }

    dstBytesPerRow = bytesPerPixel * levelWidth;
    dstBytesPerImage = dstBytesPerRow * levelHeight;
  } else {
    IGL_DEBUG_ASSERT(aspect == ImageAspectBits_Invalid || aspect == ImageAspectBits_Color,
                     "invalid aspect for color texture");
    // Destination row pitch must be at least the tight row pitch of the source region.
    // TextureFormatProperties accounts for compressed formats: rows become rows of blocks,
    // so dstBytesPerImage must use block-row count (ceil(height / blockHeight)), not pixel rows.
    const auto range = TextureRangeDesc::new2D(0, 0, (uint32_t)levelWidth, (uint32_t)levelHeight);
    dstBytesPerRow = props.getBytesPerRow(range);
    dstBytesPerImage = props.getBytesPerRange(range);
  }

  auto blitCommandEncoder = [value_ blitCommandEncoder];
  [blitCommandEncoder copyFromTexture:srcTexture
                          sourceSlice:layer
                          sourceLevel:level
                         sourceOrigin:MTLOriginMake(0, 0, 0)
                           sourceSize:MTLSizeMake(levelWidth, levelHeight, 1)
                             toBuffer:dstBuffer
                    destinationOffset:dstOffset
               destinationBytesPerRow:dstBytesPerRow
             destinationBytesPerImage:dstBytesPerImage
                                options:blitOption];
  [blitCommandEncoder endEncoding];
}

void CommandBuffer::addCompletedCallback(std::function<void(void)> callback){
  IGL_DEBUG_ASSERT(callback);
  [value_ addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
    callback();
  }];
}

void CommandBuffer::waitUntilScheduled() {
  IGL_PROFILER_FUNCTION_COLOR(IGL_PROFILER_COLOR_WAIT);
  [value_ waitUntilScheduled];
}

void CommandBuffer::waitUntilCompleted() {
  IGL_PROFILER_FUNCTION_COLOR(IGL_PROFILER_COLOR_WAIT);
  [value_ waitUntilCompleted];
}

} // namespace igl::metal
