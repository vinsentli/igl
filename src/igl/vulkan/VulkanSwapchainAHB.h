/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#pragma once

#include "VulkanSwapchain.h"

#if !USE_DEFAULT_SWAPCHAIN

#if defined(IGL_ANDROID_HWBUFFER_SUPPORTED)
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <android/surface_control.h>
#include <igl/android/UniqueFd.h>
#include <igl/vulkan/android/NativeHWBuffer.h>
#endif // defined(IGL_ANDROID_HWBUFFER_SUPPORTED)

#include <deque>
#include <mutex>

namespace igl::vulkan {

class Device;
class VulkanFence;
class VulkanSemaphore;
class VulkanSwapchain;

struct AHBFrameSynchronizerVK {
  std::shared_ptr<VulkanFence> acquireFence;
  std::shared_ptr<VulkanSemaphore> renderReady;
  std::shared_ptr<VulkanSemaphore> presentReady;
};

class SurfaceControl {
 public:
  SurfaceControl(ASurfaceControl* sc, std::shared_ptr<AHardwareBufferFunctionTable> funcTable);
  virtual ~SurfaceControl();

  ASurfaceControl* impl = nullptr;

 private:
  std::shared_ptr<AHardwareBufferFunctionTable> funcTable_;
};

class SwapchainTexture {
 public:
  explicit SwapchainTexture(std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer> texture);
  ~SwapchainTexture();

  SwapchainTexture(const SwapchainTexture&) = delete;
  SwapchainTexture& operator=(const SwapchainTexture&) = delete;
  SwapchainTexture(SwapchainTexture&&) = delete;
  SwapchainTexture& operator=(SwapchainTexture&&) = delete;

  const std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer>& texture() const {
    return texture_;
  }

 private:
  std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer> texture_;
};

class AHBTexturePool {
 public:
  struct PoolEntry {
    std::shared_ptr<SwapchainTexture> texture;
    igl::android::UniqueFd fence;

    PoolEntry() = default;
    PoolEntry(std::shared_ptr<SwapchainTexture> texture, igl::android::UniqueFd fence) :
      texture(std::move(texture)), fence(std::move(fence)) {}

    PoolEntry(const PoolEntry&) = delete;
    PoolEntry& operator=(const PoolEntry&) = delete;
    PoolEntry(PoolEntry&&) noexcept = default;
    PoolEntry& operator=(PoolEntry&&) noexcept = default;
  };

  AHBTexturePool(VulkanSwapchain& swapchain) : swapchain_(swapchain) {}
  ~AHBTexturePool();

  void push(std::shared_ptr<SwapchainTexture> texture, igl::android::UniqueFd fence);
  PoolEntry acquire(Device& device, int width, int height);
  void clear();

 private:
  VulkanSwapchain& swapchain_;
  std::mutex mutex_;
  std::deque<PoolEntry> pool_;
};

class VulkanSwapchain : public std::enable_shared_from_this<VulkanSwapchain> {
 public:
  VulkanSwapchain(VulkanContext& ctx, uint32_t width, uint32_t height);
  ~VulkanSwapchain();

  void onSurfaceCreated(ANativeWindow* nativeWindow);

  void onSurfaceDestroyed();

  void onSurfaceChanged(int width, int height);

  Result present(VkSemaphore waitSemaphore);

  std::shared_ptr<ITexture> getCurrentVulkanTexture(Device& device);

  // AHB swapchain does not support native depth attachment.
  // Caller (e.g. PlatformDevice::createTextureFromNativeDepth) must handle the
  // nullptr return and allocate its own depth texture if needed.
  // TODO: revisit if native depth via AHB becomes a requirement.
  std::shared_ptr<VulkanTexture> getCurrentDepthTexture() {
    return nullptr;
  }

  [[nodiscard]] VkSemaphore getWaitSemaphore() const noexcept;

  [[nodiscard]] VkSemaphore getSignalSemaphore() const noexcept;

  [[nodiscard]] VkFence getAcquireFence() const noexcept;

  uint32_t getWidth() const {
    return width_;
  }

  uint32_t getHeight() const {
    return height_;
  }

  VkExtent2D getExtent() const {
    return VkExtent2D{width_, height_};
  }

  VkFormat getFormatColor() const {
    return surfaceFormat_.format;
  }

  uint32_t getNumSwapchainImages() const {
    return numSwapchainImages_;
  }

  uint32_t getCurrentImageIndex() const {
    return currentImageIndex_;
  }

  uint64_t getFrameNumber() const {
    return frameNumber_;
  }

  std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer> createAHBTexture(Device& device,
                                                                                int width,
                                                                                int height);

 private:
  void submitFrameToSystem(int gpuFenceFd, VkSemaphore waitSemaphore, Result& outResult);
  std::shared_ptr<VulkanSemaphore> createSemaphoreFromFD(igl::android::UniqueFd fd);
  int exportFDFromVkSemaphore(VkSemaphore semaphore);
  void printFileDescriptorCount() const;
  void drainPendingTransactions();
  void resetSwapchainTextures();

 public:
  uint32_t numSwapchainImages_ = 0;
  uint32_t currentImageIndex_ = 0;
  uint64_t frameNumber_ = 0; // increasing continuously without bound
  VkSurfaceFormatKHR surfaceFormat_{};

 private:
  static constexpr int kMaxPendingPresents = 2;
  // Aligned with kMaxPendingPresents: caps the number of in-flight transactions on the system
  // side, preventing unbounded growth of fd / AHB / VkSemaphore resources when SurfaceFlinger
  // callbacks are delayed.
  static constexpr int kMaxInFlightTransactions = kMaxPendingPresents;
  uint32_t frameId_ = 0;
  VulkanContext& ctx_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  ANativeWindow* nativeWindow_ = nullptr;
  std::string surfaceControlName_;
  std::shared_ptr<SurfaceControl> surfaceControl_ = nullptr;

  std::vector<AHBFrameSynchronizerVK> frameSync_;
  std::shared_ptr<AHardwareBufferFunctionTable> funcTable_;
  bool getNextImage_ = true;

  std::shared_ptr<SwapchainTexture> currentAcquireTexture_;

  std::mutex currentlyDisplayedTextureMutex_;
  std::shared_ptr<SwapchainTexture> currentlyDisplayedTexture_;
  AHBTexturePool texturePool_;

  std::mutex transactionMutex_;
  int transactionCount_{0};
  std::condition_variable transactionCV_;
};

} // namespace igl::vulkan

#endif