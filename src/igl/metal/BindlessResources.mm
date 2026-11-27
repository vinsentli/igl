/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <igl/metal/BindlessResources.h>

#include <cstring>

#define IGL_COMMON_SKIP_CHECK
#include <igl/Macros.h>

namespace igl::metal {

namespace {

bool isBindlessAvailable() {
  if (@available(iOS 16.0, macOS 13.0, *)) {
    return true;
  }
  return false;
}

} // namespace

BindlessResources::BindlessResources(id<MTLDevice> device) : device_(device) {
  if (!isBindlessAvailable()) {
    return;
  }

  buffer_ = [device_ newBufferWithLength:static_cast<NSUInteger>(kMaxTextures + kMaxSamplers) *
                                         sizeof(uint64_t)
                                 options:MTLResourceStorageModeShared];
  if (!IGL_DEBUG_VERIFY(buffer_)) {
    return;
  }
  buffer_.label = @"BindlessResources";

  MTLTextureDescriptor* textureDesc =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                         width:1
                                                        height:1
                                                     mipmapped:NO];
  textureDesc.usage = MTLTextureUsageShaderRead;
  id<MTLTexture> dummyTexture = [device_ newTextureWithDescriptor:textureDesc];
  dummyTexture.label = @"BindlessResources:dummyTexture";

  MTLSamplerDescriptor* samplerDesc = [MTLSamplerDescriptor new];
  samplerDesc.label = @"BindlessResources:dummySampler";
  id<MTLSamplerState> dummySampler = [device_ newSamplerStateWithDescriptor:samplerDesc];

  // Slot 0 always holds a valid placeholder resource: released textures/samplers fall back to it so
  // shaders never sample a dangling handle.
  if (dummyTexture) {
    if (@available(iOS 16.0, macOS 13.0, *)) {
      const MTLResourceID resourceId = [dummyTexture gpuResourceID];
      std::memcpy(&dummyTextureId_, &resourceId, sizeof(dummyTextureId_));
    }
    addTexture(dummyTexture);
  }
  if (dummySampler) {
    if (@available(iOS 16.0, macOS 13.0, *)) {
      const MTLResourceID resourceId = [dummySampler gpuResourceID];
      std::memcpy(&dummySamplerId_, &resourceId, sizeof(dummySamplerId_));
    }
    addSampler(dummySampler);
  }
}

BindlessResources::~BindlessResources() = default;

uint32_t BindlessResources::addTexture(id<MTLTexture> texture) {
  if (buffer_ == nil || texture == nil) {
    return 0;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  uint32_t slot = 0;
  if (!freeTextures_.empty()) {
    slot = freeTextures_.back();
    freeTextures_.pop_back();
  } else {
    if (textures_.size() >= kMaxTextures) {
      IGL_DEBUG_ASSERT(false, "Too many bindless textures");
      return 0;
    }
    slot = static_cast<uint32_t>(textures_.size());
    textures_.emplace_back(nil);
  }

  textures_[slot] = texture;
  writeTextureId(slot, texture);
  updateResources();
  return slot;
}

void BindlessResources::removeTexture(uint32_t textureId) {
  if (buffer_ == nil || textureId == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  if (textureId >= textures_.size() || textures_[textureId] == nil) {
    return;
  }
  // The slot keeps its resource id and the texture stays alive until the GPU is done with it.
  retiredTextures_.emplace_back(RetiredTexture{textureId, frameIndex_, textures_[textureId]});
  textures_[textureId] = nil;
  updateResources();
}

uint32_t BindlessResources::addSampler(id<MTLSamplerState> sampler) {
  if (buffer_ == nil || sampler == nil) {
    return 0;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  uint32_t slot = 0;
  if (!freeSamplers_.empty()) {
    slot = freeSamplers_.back();
    freeSamplers_.pop_back();
  } else {
    if (samplers_.size() >= kMaxSamplers) {
      IGL_DEBUG_ASSERT(false, "Too many bindless samplers");
      return 0;
    }
    slot = static_cast<uint32_t>(samplers_.size());
    samplers_.emplace_back(nil);
  }

  samplers_[slot] = sampler;
  writeSamplerId(slot, sampler);
  updateResources();
  return slot;
}

void BindlessResources::removeSampler(uint32_t samplerId) {
  if (buffer_ == nil || samplerId == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  if (samplerId >= samplers_.size() || samplers_[samplerId] == nil) {
    return;
  }
  retiredSamplers_.emplace_back(RetiredSampler{samplerId, frameIndex_, samplers_[samplerId]});
  samplers_[samplerId] = nil;
  updateResources();
}

void BindlessResources::onFrameCompleted() {
  std::lock_guard<std::mutex> lock(mutex_);

  ++frameIndex_;

  for (auto it = retiredTextures_.begin(); it != retiredTextures_.end();) {
    if (frameIndex_ - it->frame < kRetiredFrames) {
      ++it;
      continue;
    }
    writeSlot(it->slot, dummyTextureId_);
    freeTextures_.push_back(it->slot);
    it = retiredTextures_.erase(it);
  }

  for (auto it = retiredSamplers_.begin(); it != retiredSamplers_.end();) {
    if (frameIndex_ - it->frame < kRetiredFrames) {
      ++it;
      continue;
    }
    writeSlot(kMaxTextures + it->slot, dummySamplerId_);
    freeSamplers_.push_back(it->slot);
    it = retiredSamplers_.erase(it);
  }
}

void BindlessResources::writeSlot(uint32_t slot, uint64_t resourceId) {
  auto* data = static_cast<uint64_t*>([buffer_ contents]);
  if (!IGL_DEBUG_VERIFY(data)) {
    return;
  }
  data[slot] = resourceId;
}

void BindlessResources::writeTextureId(uint32_t slot, id<MTLTexture> texture) {
  if (@available(iOS 16.0, macOS 13.0, *)) {
    const MTLResourceID resourceId = [texture gpuResourceID];
    uint64_t value = 0;
    std::memcpy(&value, &resourceId, sizeof(value));
    writeSlot(slot, value);
  }
}

void BindlessResources::writeSamplerId(uint32_t slot, id<MTLSamplerState> sampler) {
  if (@available(iOS 16.0, macOS 13.0, *)) {
    const MTLResourceID resourceId = [sampler gpuResourceID];
    uint64_t value = 0;
    std::memcpy(&value, &resourceId, sizeof(value));
    writeSlot(kMaxTextures + slot, value);
  }
}

void BindlessResources::updateResources() {
  // MTLSamplerState is not an MTLResource: it cannot (and does not need to) be declared resident,
  // holding a strong reference is enough.
  resources_.clear();
  for (const auto& texture : textures_) {
    if (texture) {
      resources_.emplace_back(texture);
    }
  }
}

} // namespace igl::metal
