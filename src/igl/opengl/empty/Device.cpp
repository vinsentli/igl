/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <igl/opengl/empty/Device.h>

#include <cstdio>
#include <cstring>
#include <igl/Common.h>
#include <igl/opengl/Errors.h>
#include <igl/opengl/empty/Context.h>
#include <igl/opengl/TextureBuffer.h>
#include <igl/opengl/TextureTarget.h>
#include <igl/opengl/UniformBuffer.h>

namespace igl::opengl::empty {

Device::Device(std::unique_ptr<IContext> context) :
  opengl::Device(std::move(context)), platformDevice_(*this) {}

const PlatformDevice& Device::getPlatformDevice() const noexcept {
  return platformDevice_;
}

std::shared_ptr<ITexture> Device::createTexture(const TextureDesc& desc,
                                                Result* outResult) const noexcept {
    const auto sanitized = sanitize(desc);

    std::unique_ptr<Texture> texture;
#if IGL_DEBUG
    if (sanitized.type == TextureType::TwoD || sanitized.type == TextureType::TwoDArray) {
size_t textureSizeLimit;
getFeatureLimits(DeviceFeatureLimits::MaxTextureDimension1D2D, textureSizeLimit);
IGL_ASSERT_MSG(sanitized.width <= textureSizeLimit && sanitized.height <= textureSizeLimit,
               "Texture limit size %zu is smaller than texture size %zux%zu",
               textureSizeLimit,
               sanitized.width,
               sanitized.height);
}
#endif

    if ((sanitized.usage & TextureDesc::TextureUsageBits::Sampled) != 0 ||
        (sanitized.usage & TextureDesc::TextureUsageBits::Storage) != 0) {
        texture = std::make_unique<TextureBuffer>(getContext(), desc.format);
    } else if ((sanitized.usage & TextureDesc::TextureUsageBits::Attachment) != 0) {
        if (sanitized.type == TextureType::TwoD && sanitized.numMipLevels == 1 &&
            sanitized.numLayers == 1) {
            texture = std::make_unique<TextureTarget>(getContext(), desc.format);
        } else {
            // Fall back to texture. e.g. TextureType::TwoDArray
            texture = std::make_unique<TextureBuffer>(getContext(), desc.format);
        }
    }

    if (texture != nullptr) {
        Result result = texture->create(sanitized, false);

        if (!result.isOk()) {
            //texture = nullptr;
        } else if (getResourceTracker()) {
            texture->initResourceTracker(getResourceTracker(), desc.debugName);
        }

        Result::setResult(outResult, std::move(result));
    } else {
        Result::setResult(
                outResult, Result::Code::Unsupported, "Unknown/unsupported texture usage bits.");
    }

    // sanity check to ensure that the Result value and the returned object are in sync
    // i.e. we never have a valid Result with a nullptr return value, or vice versa
    IGL_ASSERT(outResult == nullptr || (outResult->isOk() == (texture != nullptr)));

    return texture;
}

} // namespace igl::opengl::empty
