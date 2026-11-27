/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#import <Metal/Metal.h>

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>
#include <igl/Common.h>

namespace igl::metal {

/// @brief Bindless resource table for Metal.
///
/// The gpuResourceID of every texture / sampler is written into a single MTLBuffer, indexed by slot.
/// Shaders read it as `array<texture2d<float>, kMaxTextures>` + `array<sampler, kMaxSamplers>`.
class BindlessResources final {
 public:
  static constexpr uint32_t kMaxTextures = 1024;
  static constexpr uint32_t kMaxSamplers = 32;
  /// @brief The N of `[[buffer(N)]]` in shaders.
  static constexpr uint32_t kBufferIndex = 18;
  /// @brief Frames a retired slot stays frozen before it is recycled. Must be greater than the
  /// number of in-flight frames (IGL_METAL_MAX_IN_FLIGHT_BUFFERS) so the GPU can never dereference
  /// a resource id that the CPU has already replaced.
  static constexpr uint32_t kRetiredFrames = 4;

  explicit BindlessResources(id<MTLDevice> device);
  ~BindlessResources();

  BindlessResources(const BindlessResources&) = delete;
  BindlessResources& operator=(const BindlessResources&) = delete;

  [[nodiscard]] bool isValid() const {
    return buffer_ != nil;
  }

  uint32_t addTexture(id<MTLTexture> texture);
  void removeTexture(uint32_t textureId);
  uint32_t addSampler(id<MTLSamplerState> sampler);
  void removeSampler(uint32_t samplerId);

  [[nodiscard]] id<MTLBuffer> getBuffer() const {
    return buffer_;
  }

  /// @brief Invokes `callback(const id<MTLResource>* data, size_t count)` with every live texture
  /// while the lock is held, so encoders can declare them resident without racing against
  /// addTexture()/removeTexture() running on other threads.
  template <typename F>
  void forEachResource(F&& callback) const {
    std::lock_guard<std::mutex> lock(mutex_);
    callback(resources_.data(), resources_.size());
  }

  /// @brief Must be called once per frame from the thread that encodes rendering, right after the
  /// in-flight frame synchronization. Recycles slots and releases resources retired
  /// kRetiredFrames ago.
  void onFrameCompleted();

 private:
  struct RetiredTexture {
    uint32_t slot = 0;
    uint32_t frame = 0;
    id<MTLTexture> texture = nil;
  };

  struct RetiredSampler {
    uint32_t slot = 0;
    uint32_t frame = 0;
    id<MTLSamplerState> sampler = nil;
  };

  // Callers must hold mutex_.
  void writeSlot(uint32_t slot, uint64_t resourceId);
  void writeTextureId(uint32_t slot, id<MTLTexture> texture);
  void writeSamplerId(uint32_t slot, id<MTLSamplerState> sampler);
  void updateResources();

  id<MTLDevice> device_ = nil;
  id<MTLBuffer> buffer_ = nil;
  uint64_t dummyTextureId_ = 0;
  uint64_t dummySamplerId_ = 0;

  mutable std::mutex mutex_;
  uint32_t frameIndex_ = 0;
  std::vector<id<MTLTexture>> textures_;
  std::vector<uint32_t> freeTextures_;
  std::vector<RetiredTexture> retiredTextures_;
  std::vector<id<MTLSamplerState>> samplers_;
  std::vector<uint32_t> freeSamplers_;
  std::vector<RetiredSampler> retiredSamplers_;
  std::vector<__unsafe_unretained id<MTLResource>> resources_;
};

} // namespace igl::metal
