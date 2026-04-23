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
#include <igl/vulkan/android/NativeHWBuffer.h>
#endif // defined(IGL_ANDROID_HWBUFFER_SUPPORTED)

#include <deque>
#include <mutex>

namespace igl::vulkan {

class Device;
class VulkanFence;
class VulkanSemaphore;
class VulkanSwapchain;

#define WAIT_SURFACE_FLINGER 1

struct AHBFrameSynchronizerVK {
#if WAIT_SURFACE_FLINGER
  std::shared_ptr<VulkanFence> acquireFence;
  std::shared_ptr<VulkanSemaphore> renderReady;
#endif

  std::shared_ptr<VulkanSemaphore> presentReady;
};

class AHBTexturePool {
 public:
  struct PoolEntry {
    std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer> texture;
    int fence = -1;

    explicit PoolEntry(std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer> __texture,
                       int __fence) :
      texture(std::move(__texture)), fence(__fence) {}
  };

  AHBTexturePool(VulkanSwapchain& swapchain) : swapchain_(swapchain) {}
  ~AHBTexturePool();

  void Push(std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer> texture, int fence);
  PoolEntry Acquire(Device& device, int width, int height);
  void Clear();

 private:
  VulkanSwapchain& swapchain_;
  std::mutex mutex_;
  std::deque<PoolEntry> pool_;
};

class VulkanSwapchain : public std::enable_shared_from_this<VulkanSwapchain> {
 public:
  VulkanSwapchain(VulkanContext& ctx, uint32_t width, uint32_t height);
  ~VulkanSwapchain();

  void OnSurfaceCreated(ANativeWindow* nativeWindow);

  void OnSurfaceDestroyed();

  void OnSurfaceChanged(int width, int height);

  Result present(VkSemaphore waitSemaphore);

  std::shared_ptr<ITexture> getCurrentVulkanTexture(Device& device);

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

  std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer> CreateAHBTexture(Device& device,
                                                                                int width,
                                                                                int height);

 private:
  void SubmitFrameToSystem(int gpuFenceFd, VkSemaphore waitSemaphore);
  std::shared_ptr<VulkanSemaphore> CreateSemaphoreFromFD(int fd);
  int ExportFDFromVkSemaphore(VkSemaphore semaphore);
  void PrintFileDescriptorCount() const;

 public:
  uint32_t numSwapchainImages_ = 0;
  uint32_t currentImageIndex_ = 0;
  uint64_t frameNumber_ = 0; // increasing continuously without bound
  VkSurfaceFormatKHR surfaceFormat_{};

 private:
  const int kMaxPendingPresents = 2;
  uint32_t frameId_ = 0;
  VulkanContext& ctx_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  ANativeWindow* nativeWindow_ = nullptr;
  std::string surfaceControlName_;
  ASurfaceControl* surfaceControl_ = nullptr;

  ANativeWindowTransform transform_ = ANATIVEWINDOW_TRANSFORM_IDENTITY;

  std::vector<AHBFrameSynchronizerVK> frameSync_;
  std::shared_ptr<AHardwareBufferFunctionTable> funcTable_;
  bool getNextImage_ = true;

  std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer> currentAcquireTexture_;

  std::mutex currentlyDisplayedTextureMutex_;
  std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer> currentlyDisplayedTexture_;
  AHBTexturePool texturePool_;
};

#endif

} // namespace igl::vulkan