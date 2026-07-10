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
#include <igl/vulkan/VulkanSemaphore.h>
#include <igl/vulkan/VulkanTexture.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <pthread.h>
#endif

#define PRINT_FILE_DESCRIPTOR_COUNT 0

#if PRINT_FILE_DESCRIPTOR_COUNT
#include <dirent.h>
#endif

namespace igl::vulkan {

#if !USE_DEFAULT_SWAPCHAIN

ApplyThread::ApplyThread() {
  thread_ = std::thread([this] { run(); });
}

ApplyThread::~ApplyThread() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ApplyThread::post(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(task));
  }
  cv_.notify_one();
}

void ApplyThread::drain() {
  std::unique_lock<std::mutex> lock(mutex_);
  idleCv_.wait(lock, [this] { return queue_.empty() && !busy_; });
}

void ApplyThread::run() {
#if defined(__ANDROID__)
  pthread_setname_np(pthread_self(), "AHBSwapApply");
#endif
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
      if (stop_ && queue_.empty()) {
        return;
      }
      task = std::move(queue_.front());
      queue_.pop();
      busy_ = true;
    }
    task();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      busy_ = false;
    }
    idleCv_.notify_all();
  }
}

SurfaceControl::SurfaceControl(ASurfaceControl* surfaceControl,
                               std::shared_ptr<AHardwareBufferFunctionTable> funcTable) :
  impl(surfaceControl), funcTable_(funcTable) {
  IGL_DEBUG_ASSERT(impl);
  IGL_DEBUG_ASSERT(funcTable_);
}

SurfaceControl::~SurfaceControl() {
  if (impl && funcTable_) {
    funcTable_->ASurfaceControl_release(impl);
  }
}

SwapchainTexture::SwapchainTexture(
    std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer> texture) :
  texture_(std::move(texture)) {
  IGL_DEBUG_ASSERT(texture_);
  texture_->getVulkanTexture().image.isExternallyManaged_ = true;
  // FreeMemory must happen immediately on destruction, not deferred to the next frame.
  // Otherwise the AHardwareBuffer is not released right away when the app goes to background,
  // which defeats the intended behavior.
  texture_->getVulkanTexture().image.isDeferFreeMemory_ = false;
}

SwapchainTexture::~SwapchainTexture() {
  if (texture_) {
    texture_->getVulkanTexture().image.isExternallyManaged_ = false;
  }
}

AHBTexturePool::~AHBTexturePool() {
  clear();
}

void AHBTexturePool::push(std::shared_ptr<SwapchainTexture> texture, igl::android::UniqueFd fence) {
  if (!texture)
    return;

  std::lock_guard<std::mutex> lock(mutex_);
  pool_.emplace_back(std::move(texture), std::move(fence));
}

AHBTexturePool::PoolEntry AHBTexturePool::acquire(Device& device, int width, int height) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
      auto entry = std::move(pool_.front());
      pool_.pop_front();
      auto size = entry.texture->texture()->getSize();
      if (size.width == width && size.height == height) {
        return entry;
      }
    }
  }

  // no need lock
  return AHBTexturePool::PoolEntry(
      std::make_shared<SwapchainTexture>(swapchain_.createAHBTexture(device, width, height)),
      igl::android::UniqueFd{});
}

void AHBTexturePool::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  pool_.clear();
}

VulkanSwapchain::VulkanSwapchain(VulkanContext& ctx, uint32_t width, uint32_t height) :
  ctx_(ctx), width_(width), height_(height), texturePool_(*this) {
  surfaceControlName_ = IGL_FORMAT("SurfaceView[{}]", ctx_.config_.appName);
  IGL_LOG_INFO("surfaceControlName:%s", surfaceControlName_.c_str());

  nativeWindow_ = (ANativeWindow*)ctx_.window_;

  frameSync_.resize(kMaxPendingPresents);

  for (size_t i = 0; i != kMaxPendingPresents; ++i) {
    frameSync_[i].acquireFence = std::make_shared<VulkanFence>(ctx_.vf_,
                                                               ctx_.getVkDevice(),
                                                               VK_FENCE_CREATE_SIGNALED_BIT,
                                                               false,
                                                               "VulkanSwapchain_acquire_fence");
  }
}

VulkanSwapchain::~VulkanSwapchain() {
  resetSwapchainTextures(/*drainTransactions=*/true);
  surfaceControl_ = nullptr;
}

void VulkanSwapchain::drainPendingTransactions() {
  // First make sure every queued task actually ran ASurfaceTransaction_apply,
  // otherwise transactionCount_ may stay > 0 forever (apply was never issued
  // but ++ already happened in submitFrameToSystem).
  applyThread_.drain();
  std::unique_lock<std::mutex> lock(transactionMutex_);
  transactionCV_.wait(lock, [this] { return this->transactionCount_ <= 0; });
}

void VulkanSwapchain::resetSwapchainTextures(bool drainTransactions) {
  if (drainTransactions) {
    drainPendingTransactions();
  }

  texturePool_.clear();
  currentAcquireTexture_ = nullptr;
  {
    std::lock_guard<std::mutex> lock(currentlyDisplayedTextureMutex_);
    currentlyDisplayedTexture_ = nullptr;
  }
}

VkSemaphore VulkanSwapchain::getWaitSemaphore() const noexcept {
  auto sep = frameSync_[frameId_].renderReady;
  return sep ? sep->vkSemaphore_ : VK_NULL_HANDLE;
}

VkSemaphore VulkanSwapchain::getSignalSemaphore() const noexcept {
  IGL_DEBUG_ASSERT(frameSync_[frameId_].presentReady);
  return frameSync_[frameId_].presentReady->vkSemaphore_;
}

VkFence VulkanSwapchain::getAcquireFence() const noexcept {
  auto sep = frameSync_[frameId_].acquireFence;
  return sep ? sep->vkFence_ : VK_NULL_HANDLE;
}

void VulkanSwapchain::onSurfaceCreated(ANativeWindow* nativeWindow) {
  if (nativeWindow_ != nativeWindow && surfaceControl_) {
    surfaceControl_ = nullptr;
  }
  nativeWindow_ = nativeWindow;
}

void VulkanSwapchain::onSurfaceDestroyed() {
  resetSwapchainTextures(/*drainTransactions=*/false);
}

void VulkanSwapchain::onSurfaceChanged(int width, int height) {
  if (width_ != width || height_ != height) {
    resetSwapchainTextures(/*drainTransactions=*/false);
    width_ = width;
    height_ = height;
  }
}

std::shared_ptr<VulkanSemaphore> VulkanSwapchain::createSemaphoreFromFD(igl::android::UniqueFd fd) {
  if (!fd) {
    return nullptr;
  }

  auto semaphore =
      std::make_shared<VulkanSemaphore>(ctx_.vf_,
                                        ctx_.getVkDevice(),
                                        false,
                                        IGL_FORMAT("Semaphore: renderReady #{}", frameId_).c_str());

  const int rawFd = fd.get();
  VkImportSemaphoreFdInfoKHR importInfo{
      .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
      .semaphore = semaphore->vkSemaphore_,
      .fd = rawFd,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
      .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
  };

  VkResult ret = ctx_.vf_.vkImportSemaphoreFdKHR(ctx_.getVkDevice(), &importInfo);

  if (ret == VK_SUCCESS) {
    (void)fd.release();
    return semaphore;
  } else {
    IGL_LOG_ERROR("VulkanSwapchain::vkImportSemaphoreFdKHR:ret=%d, fd=%d", ret, rawFd);
    return nullptr;
  }
}

int VulkanSwapchain::exportFDFromVkSemaphore(VkSemaphore semaphore) {
  const VkSemaphoreGetFdInfoKHR getFdInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
      .semaphore = semaphore,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
  };

  int fenceFd = -1;
  const VkResult result = ctx_.vf_.vkGetSemaphoreFdKHR(ctx_.getVkDevice(), &getFdInfo, &fenceFd);
  if (result != VK_SUCCESS) {
    IGL_LOG_ERROR("VulkanSwapchain::ExportFDFromVkSemaphore failed:%d", result);
  }

  return fenceFd;
}

std::shared_ptr<igl::vulkan::android::NativeHWTextureBuffer>
VulkanSwapchain::createAHBTexture(Device& device, int width, int height) {
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
  IGL_DEBUG_ASSERT(result.isOk());
  IGL_DEBUG_ASSERT(texture);

  return texture;
}

std::shared_ptr<ITexture> VulkanSwapchain::getCurrentVulkanTexture(Device& device) {
  if (getNextImage_) {
    getNextImage_ = false;

    frameId_ = (frameId_ + 1) % kMaxPendingPresents;

    // 超时：1秒 = 1000 * 1000 * 1000 纳秒。
    if (!frameSync_[frameId_].acquireFence->wait(1000000000)){
        return nullptr;
    }
    frameSync_[frameId_].acquireFence->reset();
    frameSync_[frameId_].presentReady = std::make_shared<VulkanSemaphore>(
        ctx_.vf_,
        ctx_.getVkDevice(),
        true,
        IGL_FORMAT("Semaphore: presentReady #{}", frameId_).c_str());

    auto poolEntry = texturePool_.acquire(device, width_, height_);

    currentAcquireTexture_ = poolEntry.texture;
    IGL_DEBUG_ASSERT(currentAcquireTexture_);

    frameSync_[frameId_].renderReady = nullptr;

    if (poolEntry.fence) {
      frameSync_[frameId_].renderReady = createSemaphoreFromFD(std::move(poolEntry.fence));
    }
  }

  return currentAcquireTexture_ ? currentAcquireTexture_->texture() : nullptr;
}

Result VulkanSwapchain::present(VkSemaphore waitSemaphore) {
  getNextImage_ = true;

  IGL_DEBUG_ASSERT(frameSync_[frameId_].presentReady);
  int fenceFd = exportFDFromVkSemaphore(frameSync_[frameId_].presentReady->vkSemaphore_);
  if (fenceFd < 0) {
    return Result(Result::Code::RuntimeError,
                  "VulkanSwapchain::present: exportFDFromVkSemaphore failed");
  }

  Result result;
  submitFrameToSystem(fenceFd, waitSemaphore, result);
  return result;
}

struct TransactionInFlightData {
  VulkanSwapchain* swapchain = nullptr;
  std::weak_ptr<SurfaceControl> surfaceControl;
  std::shared_ptr<SwapchainTexture> texture;
};

void VulkanSwapchain::submitFrameToSystem(int gpuFenceFd,
                                          VkSemaphore waitSemaphore,
                                          Result& outResult) {
  igl::android::UniqueFd gpuFence{gpuFenceFd};

  auto& swapchainTexture = currentAcquireTexture_;
  IGL_DEBUG_ASSERT(swapchainTexture && swapchainTexture->texture());
  IGL_DEBUG_ASSERT(swapchainTexture->texture()->getHardwareBuffer());
  IGL_DEBUG_ASSERT(funcTable_);

#if PRINT_FILE_DESCRIPTOR_COUNT
  printFileDescriptorCount();
#endif

  if (surfaceControl_ == nullptr) {
    ASurfaceControl* sc =
        funcTable_->ASurfaceControl_createFromWindow(nativeWindow_, surfaceControlName_.c_str());
    if (sc == nullptr) {
      outResult = Result(Result::Code::RuntimeError,
                         "ASurfaceControl_createFromWindow failed");
      return;
    }
    surfaceControl_ = std::make_shared<SurfaceControl>(sc, funcTable_);
  }

  ASurfaceTransaction* transaction = funcTable_->ASurfaceTransaction_create();
  if (transaction == nullptr) {
    outResult = Result(Result::Code::RuntimeError, "ASurfaceTransaction_create failed");
    return;
  }

  funcTable_->ASurfaceTransaction_setBuffer(transaction,
                                            surfaceControl_->impl,
                                            swapchainTexture->texture()->getHardwareBuffer(),
                                            gpuFence.release());

  // Set as opaque to fix the flicker issue on Xiaomi devices when switching to background.
  funcTable_->ASurfaceTransaction_setBufferTransparency(
      transaction, surfaceControl_->impl, IglASURFACE_TRANSACTION_TRANSPARENCY_OPAQUE);

  auto inFlightData = std::make_unique<TransactionInFlightData>();
  inFlightData->swapchain = this;
  inFlightData->surfaceControl = surfaceControl_;
  inFlightData->texture = swapchainTexture;

  funcTable_->ASurfaceTransaction_setOnComplete(
      transaction, inFlightData.release(), [](void* context, ASurfaceTransactionStats* stats) {
        std::unique_ptr<TransactionInFlightData> data{
            reinterpret_cast<TransactionInFlightData*>(context)};
        auto thiz = data->swapchain;
        auto surfaceControl = data->surfaceControl.lock();
        auto texture = std::move(data->texture);

        if (!thiz) {
          IGL_DEBUG_ASSERT(false);
          return;
        }

        int releaseFd = -1;
        if (surfaceControl) {
          releaseFd = thiz->funcTable_->ASurfaceTransactionStats_getPreviousReleaseFenceFd(
              stats, surfaceControl->impl);
        }
        igl::android::UniqueFd releaseFence{releaseFd};

        std::shared_ptr<SwapchainTexture> previousTexture;
        {
          std::lock_guard<std::mutex> lock(thiz->currentlyDisplayedTextureMutex_);
          previousTexture = std::move(thiz->currentlyDisplayedTexture_);
          thiz->currentlyDisplayedTexture_ = std::move(texture);
        }
        thiz->texturePool_.push(std::move(previousTexture), std::move(releaseFence));
        {
          std::lock_guard<std::mutex> lock(thiz->transactionMutex_);
          thiz->transactionCount_--;
        }
        thiz->transactionCV_.notify_all();
      });

  {
    std::unique_lock<std::mutex> lock(transactionMutex_);
    transactionCV_.wait(lock,
                        [this] { return transactionCount_ < kMaxInFlightTransactions; });
    transactionCount_++;
  }
  // Move ASurfaceTransaction_apply / _delete off the render thread.
  // The worker is single-threaded, so transactions stay strictly FIFO on the
  // same ASurfaceControl, which is required by SurfaceFlinger.
  // Capturing `this` is safe: ~VulkanSwapchain drains applyThread_ before
  // members are destroyed, and funcTable_ is set once and never reassigned.
  applyThread_.post([this, transaction]() {
    funcTable_->ASurfaceTransaction_apply(transaction);
    funcTable_->ASurfaceTransaction_delete(transaction);
  });
}

// Print the current number of open file descriptors.
void VulkanSwapchain::printFileDescriptorCount() const {
#if PRINT_FILE_DESCRIPTOR_COUNT
  DIR* d = opendir("/proc/self/fd");
  if (d) {
    int count = 0;
    struct dirent* de;
    while ((de = readdir(d)))
      count++;
    closedir(d);
    IGL_LOG_INFO("Current open FDs: %d", count - 2); // subtract . and ..
  }
#endif
}

#endif

} // namespace igl::vulkan
