/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "VulkanSwapchainAHB.h"

#include <android/hardware_buffer.h>
#include <igl/android/NativeHWBuffer.h>
#include <igl/vulkan/Common.h>
#include <igl/vulkan/Device.h>
#include <igl/vulkan/VulkanContext.h>
#include <igl/vulkan/VulkanDevice.h>
#include <igl/vulkan/VulkanSemaphore.h>
#include <igl/vulkan/VulkanTexture.h>
#include <unistd.h>

#define PRINT_FILE_DESCRIPTOR_COUNT 0

#if PRINT_FILE_DESCRIPTOR_COUNT
#include <dirent.h>
#endif

namespace igl::vulkan {

#if !USE_DEFAULT_SWAPCHAIN

AHBTexturePool::~AHBTexturePool() {
  Clear();
}

void AHBTexturePool::Push(std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer> texture,
                          int fence) {
  if (!texture)
    return;

  std::lock_guard<std::mutex> lock(mutex_);
  pool_.emplace_back(std::move(texture), fence);
}

AHBTexturePool::PoolEntry AHBTexturePool::Acquire(Device& device, int width, int height) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
      auto entry = pool_.front();
      pool_.pop_front();
      auto size = entry.texture->getSize();
      if (size.width == width && size.height == height) {
        // 标记这是一个交换链Texture，才能进行Present.
        entry.texture->getVulkanTexture().image_.isExternallyManaged_ = true;
        return entry;
      }
    }
  }

  // no need lock
  return AHBTexturePool::PoolEntry(swapchain_.CreateAHBTexture(device, width, height), -1);
}

void AHBTexturePool::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);

  // 需要将isExternallyManaged_置成false，否则VulkanImage不会释放。
  for (auto& entry : pool_) {
    entry.texture->getVulkanTexture().image_.isExternallyManaged_ = false;
  }

  pool_.clear();
}

VulkanSwapchain::VulkanSwapchain(VulkanContext& ctx, uint32_t width, uint32_t height) :
  ctx_(ctx), width_(width), height_(height), texturePool_(*this) {
  surfaceControlName_ = IGL_FORMAT("SurfaceView[{}]", ctx_.config_.appName);
  IGL_LOG_INFO("surfaceControlName:%s", surfaceControlName_.c_str());

  nativeWindow_ = (ANativeWindow*)ctx_.window_;

  frameSync_.resize(kMaxPendingPresents);

#if WAIT_SURFACE_FLINGER
  for (size_t i = 0; i != kMaxPendingPresents; ++i) {
    frameSync_[i].acquireFence = std::make_shared<VulkanFence>(ctx_.vf_,
                                                               ctx_.device_->device_,
                                                               VK_FENCE_CREATE_SIGNALED_BIT,
                                                               false,
                                                               "VulkanSwapchain_acquire_fence");
  }
#endif
}

VulkanSwapchain::~VulkanSwapchain() {
  IGL_DEBUG_ASSERT(funcTable_);

  if (surfaceControl_ && funcTable_) {
    funcTable_->ASurfaceControl_release(surfaceControl_);
  }

  texturePool_.Clear();
}

VkSemaphore VulkanSwapchain::getWaitSemaphore() const noexcept {
#if WAIT_SURFACE_FLINGER
  auto sep = frameSync_[frameId_].renderReady;
  return sep ? sep->vkSemaphore_ : VK_NULL_HANDLE;
#else
  return VK_NULL_HANDLE;
#endif
}

VkSemaphore VulkanSwapchain::getSignalSemaphore() const noexcept {
  auto sep = frameSync_[frameId_].presentReady;
  return sep ? sep->vkSemaphore_ : VK_NULL_HANDLE;
}

VkFence VulkanSwapchain::getAcquireFence() const noexcept {
#if WAIT_SURFACE_FLINGER
  auto sep = frameSync_[frameId_].acquireFence;
  return sep ? sep->vkFence_ : VK_NULL_HANDLE;
#else
  return VK_NULL_HANDLE;
#endif
}

void VulkanSwapchain::OnSurfaceCreated(ANativeWindow* nativeWindow) {
  if (nativeWindow_ != nativeWindow && surfaceControl_ && funcTable_) {
    funcTable_->ASurfaceControl_release(surfaceControl_);
    surfaceControl_ = nullptr;
  }
  nativeWindow_ = nativeWindow;
}

void VulkanSwapchain::OnSurfaceDestroyed() {
  texturePool_.Clear();

  currentAcquireTexture_ = nullptr;

  {
    std::lock_guard<std::mutex> lock(currentlyDisplayedTextureMutex_);
    currentlyDisplayedTexture_ = nullptr;
  }
}

void VulkanSwapchain::OnSurfaceChanged(int width, int height) {
  IGL_DEBUG_ASSERT(width_ != width || height_ != height);

  width_ = width;
  height_ = height;

  texturePool_.Clear();
}

std::shared_ptr<VulkanSemaphore> VulkanSwapchain::CreateSemaphoreFromFD(int fd) {
  auto semaphore =
      std::make_shared<VulkanSemaphore>(ctx_.vf_,
                                        ctx_.device_->device_,
                                        false,
                                        IGL_FORMAT("Semaphore: renderReady #{}", frameId_).c_str());

  VkImportSemaphoreFdInfoKHR importInfo{
      .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
      .semaphore = semaphore->vkSemaphore_,
      .fd = fd,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
      .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
  };

  VkResult ret = ctx_.vf_.vkImportSemaphoreFdKHR(ctx_.device_->device_, &importInfo);

  if (ret == VK_SUCCESS) {
    IGL_LOG_INFO("VulkanSwapchain::vkImportSemaphoreFdKHR:ret=%d, dup fd=%d", ret, fd);
    return semaphore;
  } else {
    IGL_LOG_ERROR("VulkanSwapchain::vkImportSemaphoreFdKHR:ret=%d, dup fd=%d", ret, fd);
    close(fd);
    return nullptr;
  }
}

int VulkanSwapchain::ExportFDFromVkSemaphore(VkSemaphore semaphore) {
  const VkSemaphoreGetFdInfoKHR getFdInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
      .semaphore = semaphore,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
  };

  int fenceFd = -1;
  const VkResult result = ctx_.vf_.vkGetSemaphoreFdKHR(ctx_.device_->device_, &getFdInfo, &fenceFd);
  if (result != VK_SUCCESS) {
    IGL_LOG_ERROR("VulkanSwapchain::ExportFDFromVkSemaphore failed:%d", result);
  }

  return fenceFd;
}

std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer>
VulkanSwapchain::CreateAHBTexture(Device& device, int width, int height) {
  auto& platformDevice = device.getPlatformDevice();

  funcTable_ = platformDevice.getFunctionTable();

  Result result;

  TextureDesc desc = TextureDesc::new2D(ctx_.config_.requestedSwapChainTextureFormat,
                                        width,
                                        height,
                                        igl::TextureDesc::TextureUsageBits::Attachment |
                                            igl::TextureDesc::TextureUsageBits::Sampled);
  desc.storage = igl::ResourceStorage::Shared;
  desc.debugName = "VulkanSwapchainAHB Image";
  auto texture = std::dynamic_pointer_cast<igl::vulkan::android::NativeHWTextureBuffer>(
      platformDevice.createTextureWithSharedMemory(desc, &result));
  assert(result.isOk());
  assert(texture);

  // 析构时要立即FreeMemory，不要延迟到下一帧，否则切到后台时，AHBuffer不会立即释放，达不到预期效果。
  texture->getVulkanTexture().image_.isDeferFreeMemory_ = false;
  // 标记这是一个交换链Texture，才能进行Present.
  texture->getVulkanTexture().image_.isExternallyManaged_ = true;

  return texture;
}

std::shared_ptr<ITexture> VulkanSwapchain::getCurrentVulkanTexture(Device& device) {
  if (getNextImage_) {
    getNextImage_ = false;

    frameId_ = (frameId_ + 1) % kMaxPendingPresents;

#if WAIT_SURFACE_FLINGER
    IGL_LOG_INFO("VulkanSwapchain------------------------------------------------");
    IGL_LOG_INFO("VulkanSwapchain::getCurrentVulkanTexture, acquireFence wait");
    frameSync_[frameId_].acquireFence->wait();
    IGL_LOG_INFO("VulkanSwapchain::getCurrentVulkanTexture, acquireFence wait finish");

    frameSync_[frameId_].acquireFence->reset();
    IGL_LOG_INFO("VulkanSwapchain::getCurrentVulkanTexture, acquireFence reset finish");
#endif
    frameSync_[frameId_].presentReady = std::make_shared<VulkanSemaphore>(
        ctx_.vf_,
        ctx_.device_->device_,
        true,
        IGL_FORMAT("Semaphore: presentReady #{}", frameId_).c_str());

    auto poolEntry = texturePool_.Acquire(device, width_, height_);

    currentAcquireTexture_ = poolEntry.texture;
    IGL_DEBUG_ASSERT(currentAcquireTexture_);

    frameSync_[frameId_].renderReady = nullptr;

    if (poolEntry.fence >= 0) {
#if WAIT_SURFACE_FLINGER
      frameSync_[frameId_].renderReady = CreateSemaphoreFromFD(poolEntry.fence);
#endif
    }
  }

  return currentAcquireTexture_;
}

Result VulkanSwapchain::present(VkSemaphore waitSemaphore) {
  getNextImage_ = true;

  int fenceFd = ExportFDFromVkSemaphore(frameSync_[frameId_].presentReady->vkSemaphore_);
  SubmitFrameToSystem(fenceFd, waitSemaphore);

  return Result();
}

struct TransactionInFlightData {
  std::weak_ptr<VulkanSwapchain> swapchain;
  std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer> texture;
};

void VulkanSwapchain::SubmitFrameToSystem(int gpuFenceFd, VkSemaphore waitSemaphore) {
  auto& texture = currentAcquireTexture_;
  IGL_DEBUG_ASSERT(texture && texture->getHardwareBuffer());
  IGL_DEBUG_ASSERT(funcTable_);

#if PRINT_FILE_DESCRIPTOR_COUNT
  PrintFileDescriptorCount();
#endif

  // 1. 初始化 SurfaceControl (通常只需创建一次，建议全局保存)
  if (surfaceControl_ == nullptr) {
    // 将 ANativeWindow 包装成可受事务控制的 ASurfaceControl
    surfaceControl_ =
        funcTable_->ASurfaceControl_createFromWindow(nativeWindow_, surfaceControlName_.c_str());
    IGL_DEBUG_ASSERT(surfaceControl_);
  }

  // 2. 创建一个原子事务
  ASurfaceTransaction* transaction = funcTable_->ASurfaceTransaction_create();

  // 3. 设置 Buffer 及其同步栅栏
  // 这里的 gpuFenceFd 告诉系统：请等这个 Fence 信号发出后（GPU画完），再把 Buffer 显示出来
  funcTable_->ASurfaceTransaction_setBuffer(
      transaction, surfaceControl_, texture->getHardwareBuffer(), gpuFenceFd);

  // 4. 配置显示属性 (可选，但常用)
#if 0 // 不需要设置旋转，效果正常
    ARect source{
        .left = 0,
        .top = 0,
        .right = (int32_t)width_,
        .bottom = (int32_t)height_,
    };

    ASurfaceTransaction_setGeometry(transaction, rootControl_, source, source, transform_);
#endif

  TransactionInFlightData* inFlightData = nullptr;
#if WAIT_SURFACE_FLINGER
  inFlightData = new TransactionInFlightData();
  inFlightData->swapchain = weak_from_this();
  inFlightData->texture = texture;
#endif

  // 5. 设置完成回调 (非常重要：用于 Buffer 回收)
  // 系统显示完这个 Buffer 后，会通过回调告知你，这样你才能把 AHB 放回池子里给 Vulkan 重复使用
  funcTable_->ASurfaceTransaction_setOnComplete(
      transaction, inFlightData, [](void* context, ASurfaceTransactionStats* stats) {
  // 在这里处理 Buffer 释放或统计逻辑
  // 注意：这是在另一个线程异步触发的
#if WAIT_SURFACE_FLINGER
        IGL_LOG_INFO("VulkanSwapchain::ASurfaceTransaction_setOnComplete");
        auto data = reinterpret_cast<TransactionInFlightData*>(context);
        auto thiz = data->swapchain.lock();
        auto texture = std::move(data->texture);
        // 防止VulkanImage不能释放
        texture->getVulkanTexture().image_.isExternallyManaged_ = false;
        delete data;

        if (!thiz)
          return;

        int releaseFd = thiz->funcTable_->ASurfaceTransactionStats_getPreviousReleaseFenceFd(
            stats, thiz->surfaceControl_);
        IGL_LOG_INFO("VulkanSwapchain getPreviousReleaseFenceFd:%d", releaseFd);

        std::lock_guard<std::mutex> lock(thiz->currentlyDisplayedTextureMutex_);
        auto previous_texture = thiz->currentlyDisplayedTexture_;
        thiz->currentlyDisplayedTexture_ = std::move(texture);

        thiz->texturePool_.Push(previous_texture, releaseFd);
#endif
      });

  // 6. 应用并提交事务
  // 一旦 apply，系统合成器 SurfaceFlinger 就会接管这个 Buffer
  funcTable_->ASurfaceTransaction_apply(transaction);

  // 7. 清理事务对象 (apply 后即可删除事务描述符，不会影响已提交的 Buffer)
  funcTable_->ASurfaceTransaction_delete(transaction);
}

// 打印当前创建的句柄数量
void VulkanSwapchain::PrintFileDescriptorCount() const {
#if PRINT_FILE_DESCRIPTOR_COUNT
  DIR* d = opendir("/proc/self/fd");
  if (d) {
    int count = 0;
    struct dirent* de;
    while ((de = readdir(d)))
      count++;
    closedir(d);
    IGL_LOG_INFO("Current open FDs: %d", count - 2); // 减去 . 和 ..
  }
#endif
}

#endif

} // namespace igl::vulkan