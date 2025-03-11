/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <igl/opengl/ViewTextureTarget.h>
#include <igl/opengl/egl/Context.h>
#include <igl/opengl/egl/Device.h>
#include <igl/opengl/egl/PlatformDevice.h>
#if defined(IGL_ANDROID_HWBUFFER_SUPPORTED)
#include <android/hardware_buffer.h>
#include <igl/opengl/egl/android/NativeHWBuffer.h>
#endif // defined(IGL_ANDROID_HWBUFFER_SUPPORTED)
#include <sstream>
#include <utility>

namespace igl::opengl::egl {

PlatformDevice::PlatformDevice(Device& owner) : opengl::PlatformDevice(owner) {
#if defined(IGL_ANDROID_HWBUFFER_SUPPORTED)
    funcTable_ = AHardwareBufferFunctionTable::create();
#endif
}

std::shared_ptr<ITexture> PlatformDevice::createTextureFromNativeDrawable(
    TextureFormat colorTextureFormat,
    Result* outResult) {
  if (drawableTexture_) {
    return drawableTexture_;
  }

  auto* context = static_cast<Context*>(getSharedContext().get());
  if (context == nullptr) {
    Result::setResult(outResult, Result::Code::InvalidOperation, "No EGL context found!");
    return nullptr;
  }

  Result subResult;
  auto dimensions = context->getDrawSurfaceDimensions(&subResult);
  if (!subResult.isOk()) {
    Result::setResult(outResult, subResult.code, subResult.message);
    return nullptr;
  }

  const TextureDesc desc = {
      dimensions.first < 0 ? 0 : static_cast<uint32_t>(dimensions.first),
      dimensions.second < 0 ? 0 : static_cast<uint32_t>(dimensions.second),
      1, // depth
      1, // numLayers
      1, // numSamples
      TextureDesc::TextureUsageBits::Attachment,
      1, // numMipLevels
      TextureType::TwoD,
      colorTextureFormat,
      ResourceStorage::Private,
  };
  auto texture = std::make_shared<ViewTextureTarget>(getContext(), desc.format);
  subResult = texture->create(desc, true);
  Result::setResult(outResult, subResult.code, subResult.message);
  if (!subResult.isOk()) {
    return nullptr;
  }
  drawableTexture_ = std::move(texture);
  if (auto resourceTracker = owner_.getResourceTracker()) {
    drawableTexture_->initResourceTracker(resourceTracker, "TextureFromNativeDrawable");
  }

  return drawableTexture_;
}

std::shared_ptr<ITexture> PlatformDevice::createTextureFromNativeDrawable(
    int width,
    int height,
    TextureFormat colorTextureFormat,
    Result* outResult) {
  if (drawableTexture_ && drawableTexture_->getWidth() == width &&
      drawableTexture_->getHeight() == height) {
    return drawableTexture_;
  }

  auto* context = static_cast<Context*>(getSharedContext().get());
  if (context == nullptr) {
    Result::setResult(outResult, Result::Code::InvalidOperation, "No EGL context found!");
    return nullptr;
  }

  const TextureDesc desc = {
      static_cast<uint32_t>(width),
      static_cast<uint32_t>(height),
      1, // depth
      1, // numLayers
      1, // numSamples
      TextureDesc::TextureUsageBits::Attachment,
      1, // numMipLevels
      TextureType::TwoD,
      colorTextureFormat,
      ResourceStorage::Private,
  };
  auto texture = std::make_shared<ViewTextureTarget>(getContext(), desc.format);
  const Result subResult = texture->create(desc, true);
  Result::setResult(outResult, subResult.code, subResult.message);
  if (!subResult.isOk()) {
    return nullptr;
  }
  drawableTexture_ = std::move(texture);
  if (auto resourceTracker = owner_.getResourceTracker()) {
    drawableTexture_->initResourceTracker(resourceTracker, "TextureFromNativeDrawable");
  }

  return drawableTexture_;
}

std::shared_ptr<ITexture> PlatformDevice::createTextureFromNativeDepth(
    TextureFormat depthTextureFormat,
    Result* outResult) {
  if (depthTexture_ && depthTexture_->getFormat() == depthTextureFormat) {
    return depthTexture_;
  }

  auto* context = static_cast<Context*>(getSharedContext().get());
  if (context == nullptr) {
    Result::setResult(outResult, Result::Code::InvalidOperation, "No EGL context found!");
    return nullptr;
  }

  Result subResult;
  auto dimensions = context->getDrawSurfaceDimensions(&subResult);
  if (!subResult.isOk()) {
    Result::setResult(outResult, subResult.code, subResult.message);
    return nullptr;
  }

  const TextureDesc desc = {
      dimensions.first < 0 ? 0 : static_cast<uint32_t>(dimensions.first),
      dimensions.second < 0 ? 0 : static_cast<uint32_t>(dimensions.second),
      1, // depth
      1, // numLayers
      1, // numSamples
      TextureDesc::TextureUsageBits::Attachment,
      1, // numMipLevels
      TextureType::TwoD,
      depthTextureFormat,
      ResourceStorage::Private,
  };
  auto texture = std::make_shared<ViewTextureTarget>(getContext(), desc.format);
  subResult = texture->create(desc, true);
  Result::setResult(outResult, subResult.code, subResult.message);
  if (!subResult.isOk()) {
    return nullptr;
  }

  depthTexture_ = std::move(texture);
  if (auto resourceTracker = owner_.getResourceTracker()) {
    depthTexture_->initResourceTracker(std::move(resourceTracker), "TextureFromNativeDepth");
  }

  return depthTexture_;
}

#if defined(IGL_ANDROID_HWBUFFER_SUPPORTED)
/// returns a android::NativeHWTextureBuffer on platforms supporting it
/// this texture allows CPU and GPU to both read/write memory
std::shared_ptr<ITexture> PlatformDevice::createTextureWithSharedMemory(const TextureDesc& desc,
                                                                        Result* outResult) const {
  if (!funcTable_) {
      return nullptr;
  }

  auto context = static_cast<Context*>(getSharedContext().get());
  if (context == nullptr) {
    Result::setResult(outResult, Result::Code::InvalidOperation, "No EGL context found!");
    IGL_LOG_ERROR("No EGL context found!");
    return nullptr;
  }

  Result subResult;

  auto texture = std::make_shared<android::NativeHWTextureBuffer>(getContext(), funcTable_.get(),  desc.format);
  subResult = texture->createHWBuffer(desc, false, false);
  texture->setTextureUsage(desc.usage);
  Result::setResult(outResult, subResult.code, subResult.message);
  if (!subResult.isOk()) {
    IGL_LOG_ERROR("sub result failed");
    return nullptr;
  }

  if (auto resourceTracker = owner_.getResourceTracker()) {
    texture->initResourceTracker(resourceTracker);
  }

  return texture;
}

std::shared_ptr<ITexture> PlatformDevice::createTextureWithSharedMemory(AHardwareBuffer* buffer,
                                                                        Result* outResult) const {
  if (!funcTable_) {
      return nullptr;
  }

  auto context = static_cast<Context*>(getSharedContext().get());
  if (context == nullptr) {
    Result::setResult(outResult, Result::Code::InvalidOperation, "No EGL context found!");
    return nullptr;
  }

  Result subResult;

  AHardwareBuffer_Desc hwbDesc;
  funcTable_->AHardwareBuffer_describe(buffer, &hwbDesc);

  auto texture = std::make_shared<android::NativeHWTextureBuffer>(
      getContext(), funcTable_.get(), igl::android::getIglFormat(hwbDesc.format));
  subResult = texture->createWithHWBuffer(buffer);
  Result::setResult(outResult, subResult.code, subResult.message);
  if (!subResult.isOk()) {
    return nullptr;
  }

  if (auto resourceTracker = owner_.getResourceTracker()) {
    texture->initResourceTracker(resourceTracker);
  }

  return texture;
}
#endif // defined(IGL_ANDROID_HWBUFFER_SUPPORTED)

void PlatformDevice::updateSurfaces(EGLSurface readSurface,
                                    EGLSurface drawSurface,
                                    Result* outResult) {
  auto* context = static_cast<Context*>(getSharedContext().get());
  if (context == nullptr) {
    Result::setResult(outResult, Result::Code::InvalidOperation, "No EGL context found!");
    return;
  }
  context->updateSurfaces(readSurface, drawSurface);

  if (drawableTexture_ != nullptr) {
    auto dimensions = context->getDrawSurfaceDimensions(outResult);

    drawableTexture_->setTextureProperties(dimensions.first, dimensions.second);
  }
}

EGLSurface PlatformDevice::createSurface(NativeWindowType nativeWindow, Result* outResult) {
  auto* context = static_cast<Context*>(getSharedContext().get());
  if (context == nullptr) {
    Result::setResult(outResult, Result::Code::InvalidOperation, "No EGL context found!");
    return nullptr;
  }
  return context->createSurface(nativeWindow);
}

EGLSurface PlatformDevice::getReadSurface(Result* outResult) {
  auto* context = static_cast<Context*>(getSharedContext().get());
  if (context == nullptr) {
    Result::setResult(outResult, Result::Code::InvalidOperation, "No EGL context found!");
    return nullptr;
  }
  return context->getReadSurface();
}

void PlatformDevice::setPresentationTime(long long presentationTimeNs, Result* outResult) {
  auto* context = static_cast<Context*>(getSharedContext().get());
  if (context == nullptr) {
    Result::setResult(outResult, Result::Code::InvalidOperation, "No EGL context found!");
    return;
  }
  return context->setPresentationTime(presentationTimeNs);
}

bool PlatformDevice::isType(PlatformDeviceType t) const noexcept {
  return t == Type || opengl::PlatformDevice::isType(t);
}

} // namespace igl::opengl::egl
