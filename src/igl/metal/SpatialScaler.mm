/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <igl/metal/SpatialScaler.h>

#ifndef NOT_USE_UPSCALER

#import <Metal/Metal.h>
#include <igl/metal/CommandBuffer.h>
#include <igl/metal/Device.h>
#include <igl/metal/Texture.h>

#if IGL_METALFX_AVAILABLE
#import <MetalFX/MetalFX.h>
#endif

namespace igl::metal {

namespace {

#if IGL_METALFX_AVAILABLE

/**
 * @brief Converts IGL texture format to Metal pixel format.
 */
MTLPixelFormat toMTLPixelFormat(TextureFormat format) {
  switch (format) {
  case TextureFormat::BGRA_UNorm8:
    return MTLPixelFormatBGRA8Unorm;
  case TextureFormat::RGBA_UNorm8:
    return MTLPixelFormatRGBA8Unorm;
  case TextureFormat::BGRA_SRGB:
    return MTLPixelFormatBGRA8Unorm_sRGB;
  case TextureFormat::RGBA_SRGB:
    return MTLPixelFormatRGBA8Unorm_sRGB;
  case TextureFormat::RG_F16:
    return MTLPixelFormatRG16Float;
  case TextureFormat::RGBA_F16:
    return MTLPixelFormatRGBA16Float;
  default:
    return MTLPixelFormatBGRA8Unorm;
  }
}

/**
 * @brief Checks if the runtime iOS/macOS version supports MetalFX.
 */
bool isMetalFXRuntimeAvailable() {
  if (@available(iOS 16.0, macOS 13.0, tvOS 16.0, *)) {
    return true;
  }
  return false;
}

#endif  // IGL_METALFX_AVAILABLE

}  // namespace

#ifdef __OBJC__
bool SpatialScaler::isSupported(id<MTLDevice> IGL_NONNULL device) {
#if IGL_METALFX_AVAILABLE
  if (@available(iOS 16.0, macOS 13.0, tvOS 16.0, *)) {
    // ★ supportsDevice: 是类方法，用于检查设备是否支持 MetalFX 空间超分
    // 返回 YES 表示该设备支持创建 MTLFXSpatialScaler
    return [MTLFXSpatialScalerDescriptor supportsDevice:device];
  }
#endif
  return false;
}
#endif

SpatialScaler::SpatialScaler(id<MTLDevice> mtlDevice,
                             const SpatialScalerDesc& desc,
                             Result* outResult)
    : desc_(desc) {
  // Retain the device for later use (creating internal textures)
  mtlDevice_ = (__bridge_retained void*)mtlDevice;

#if IGL_METALFX_AVAILABLE
  if (@available(iOS 16.0, macOS 13.0, tvOS 16.0, *)) {
    if (!isSupported(mtlDevice)) {
      Result::setResult(outResult,
                        Result::Code::Unsupported,
                        "MetalFX not supported on this device (requires iOS 16+/A14+)");
      return;
    }

    if (!desc.isValid()) {
      Result::setResult(outResult,
                        Result::Code::ArgumentInvalid,
                        "Invalid SpatialScalerDesc: dimensions must be positive and input <= output");
      return;
    }

    // Create MetalFX spatial scaler descriptor
    MTLFXSpatialScalerDescriptor* scalerDesc = [[MTLFXSpatialScalerDescriptor alloc] init];
    scalerDesc.inputWidth = desc.inputWidth;
    scalerDesc.inputHeight = desc.inputHeight;
    scalerDesc.outputWidth = desc.outputWidth;
    scalerDesc.outputHeight = desc.outputHeight;
    scalerDesc.colorTextureFormat = toMTLPixelFormat(desc.colorFormat);
    scalerDesc.outputTextureFormat = toMTLPixelFormat(desc.colorFormat);

    // Create the scaler
    id<MTLFXSpatialScaler> scaler = [scalerDesc newSpatialScalerWithDevice:mtlDevice];

    if (scaler == nil) {
      Result::setResult(outResult,
                        Result::Code::RuntimeError,
                        "Failed to create MTLFXSpatialScaler");
      return;
    }

    // Store as void* to avoid API availability issues in header
    scaler_ = (__bridge_retained void*)scaler;

    Result::setOk(outResult);
    return;
  }
#endif

  Result::setResult(outResult,
                    Result::Code::Unsupported,
                    "MetalFX not available (requires iOS 16.0+/macOS 13.0+)");
}

SpatialScaler::~SpatialScaler() {
#if IGL_METALFX_AVAILABLE
  if (scaler_) {
    // Release the retained object
    CFRelease(scaler_);
    scaler_ = nullptr;
  }
  if (internalOutputTexture_) {
    CFRelease(internalOutputTexture_);
    internalOutputTexture_ = nullptr;
  }
#endif
  if (mtlDevice_) {
    CFRelease(mtlDevice_);
    mtlDevice_ = nullptr;
  }
}

Result SpatialScaler::setInputTexture(const std::shared_ptr<ITexture>& texture) {
  if (!texture) {
    return Result(Result::Code::ArgumentNull, "Input texture is null");
  }

  // Validate dimensions
  if (texture->getDimensions().width != desc_.inputWidth ||
      texture->getDimensions().height != desc_.inputHeight) {
    return Result(Result::Code::ArgumentInvalid,
                  "Input texture dimensions don't match descriptor");
  }

  inputTexture_ = texture;
  return Result();
}

Result SpatialScaler::setOutputTexture(const std::shared_ptr<ITexture>& texture) {
  if (!texture) {
    return Result(Result::Code::ArgumentNull, "Output texture is null");
  }

  // Validate dimensions
  if (texture->getDimensions().width != desc_.outputWidth ||
      texture->getDimensions().height != desc_.outputHeight) {
    return Result(Result::Code::ArgumentInvalid,
                  "Output texture dimensions don't match descriptor");
  }

  outputTexture_ = texture;
  return Result();
}

Result SpatialScaler::encode(ICommandBuffer& commandBuffer) {
#if IGL_METALFX_AVAILABLE
  if (@available(iOS 16.0, macOS 13.0, tvOS 16.0, *)) {
    if (!isValid()) {
      return Result(Result::Code::InvalidOperation, "Spatial scaler is not valid");
    }

    if (!inputTexture_) {
      return Result(Result::Code::InvalidOperation, "Input texture not set");
    }

    if (!outputTexture_) {
      return Result(Result::Code::InvalidOperation, "Output texture not set");
    }

    // Get Metal command buffer
    auto& metalCommandBuffer = static_cast<CommandBuffer&>(commandBuffer);
    id<MTLCommandBuffer> mtlCommandBuffer = metalCommandBuffer.get();

    if (mtlCommandBuffer == nil) {
      return Result(Result::Code::RuntimeError, "Failed to get MTLCommandBuffer");
    }

    // Get Metal textures
    auto* inputMetalTexture = static_cast<Texture*>(inputTexture_.get());
    auto* outputMetalTexture = static_cast<Texture*>(outputTexture_.get());

    if (!inputMetalTexture || !outputMetalTexture) {
      return Result(Result::Code::RuntimeError, "Failed to get Metal textures");
    }

    id<MTLTexture> inputMTLTexture = inputMetalTexture->get();
    id<MTLTexture> outputMTLTexture = outputMetalTexture->get();

    if (inputMTLTexture == nil || outputMTLTexture == nil) {
      return Result(Result::Code::RuntimeError, "Metal textures are nil");
    }

    // Cast void* back to id<MTLFXSpatialScaler>
    id<MTLFXSpatialScaler> scaler = (__bridge id<MTLFXSpatialScaler>)scaler_;

    // ★ 检查 output texture 是否已有 ShaderWrite usage
    // 如果有，MetalFX 可以直接输出到它，省掉 internal texture 和 blit
    bool outputHasShaderWrite = (outputMTLTexture.usage & MTLTextureUsageShaderWrite) != 0;

    if (outputHasShaderWrite) {
      // ★ 优化路径：output texture 已有 ShaderWrite，直接输出
      scaler.colorTexture = inputMTLTexture;
      scaler.outputTexture = outputMTLTexture;
      [scaler encodeToCommandBuffer:mtlCommandBuffer];
    } else {
      // ★ 兼容路径：output texture 无 ShaderWrite，走 internal texture + blit
      id<MTLDevice> device = (__bridge id<MTLDevice>)mtlDevice_;
      if (!device) {
        return Result(Result::Code::RuntimeError, "MTLDevice is nil");
      }

      uint32_t outW = (uint32_t)outputMTLTexture.width;
      uint32_t outH = (uint32_t)outputMTLTexture.height;

      // Create or recreate internal output texture if needed
      if (!internalOutputTexture_ ||
          internalOutputWidth_ != outW ||
          internalOutputHeight_ != outH) {
        // Release old texture
        if (internalOutputTexture_) {
          CFRelease(internalOutputTexture_);
          internalOutputTexture_ = nullptr;
        }

        MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
        desc.textureType = MTLTextureType2D;
        desc.pixelFormat = outputMTLTexture.pixelFormat;
        desc.width = outW;
        desc.height = outH;
        desc.mipmapLevelCount = 1;
        desc.sampleCount = 1;
        desc.storageMode = MTLStorageModePrivate;
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;

        id<MTLTexture> internalTex = [device newTextureWithDescriptor:desc];
        if (!internalTex) {
          return Result(Result::Code::RuntimeError, "Failed to create internal output texture with ShaderWrite");
        }
        internalTex.label = @"MetalFX_InternalOutput";

        internalOutputTexture_ = (__bridge_retained void*)internalTex;
        internalOutputWidth_ = outW;
        internalOutputHeight_ = outH;
      }

      id<MTLTexture> internalOutTex = (__bridge id<MTLTexture>)internalOutputTexture_;

      scaler.colorTexture = inputMTLTexture;
      scaler.outputTexture = internalOutTex;
      [scaler encodeToCommandBuffer:mtlCommandBuffer];

      // Blit from internal output texture to the external output texture
      id<MTLBlitCommandEncoder> blitEncoder = [mtlCommandBuffer blitCommandEncoder];
      if (blitEncoder) {
        blitEncoder.label = @"MetalFX_BlitToOutput";
        [blitEncoder copyFromTexture:internalOutTex
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(outW, outH, 1)
                           toTexture:outputMTLTexture
                    destinationSlice:0
                    destinationLevel:0
                   destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blitEncoder endEncoding];
      }
    }

    return Result();
  }
#endif

  return Result(Result::Code::Unsupported, "MetalFX not available");
}

std::shared_ptr<ITexture> SpatialScaler::getInputTexture() const {
  return inputTexture_;
}

std::shared_ptr<ITexture> SpatialScaler::getOutputTexture() const {
  return outputTexture_;
}

const SpatialScalerDesc& SpatialScaler::getDesc() const {
  return desc_;
}

bool SpatialScaler::isValid() const {
#if IGL_METALFX_AVAILABLE
  return scaler_ != nullptr;
#else
  return false;
#endif
}

} // namespace igl::metal

#endif
