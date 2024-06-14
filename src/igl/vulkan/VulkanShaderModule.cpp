/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <igl/vulkan/VulkanShaderModule.h>

<<<<<<< HEAD
#if IGL_USE_GLSLANG
#include <glslang/Include/glslang_c_interface.h>
#endif

#include <igl/vulkan/Common.h>

namespace {

// Logs GLSL shaders with line numbers annotation
void logShaderSource(const char* text) {
#if IGL_DEBUG
  uint32_t line = 1;

  // IGLLog on Android also writes a new line,
  // so to make things easier to read separate out the Android logging
#if IGL_PLATFORM_ANDROID
  std::string outputLine = "";
  while (text && *text) {
    if (*text == '\n') {
      // Write out the line along with the line number, and reset the line
      IGL_LOG_INFO("(%3u) %s", line++, outputLine.c_str());
      outputLine = "";
    } else if (*text == '\r') {
      // skip it
    } else {
      outputLine += *text;
    }
    text++;
  }
  IGL_LOG_INFO("");
#else
  IGL_LOG_INFO("\n(%3u) ", line);
  while (text && *text) {
    if (*text == '\n') {
      IGL_LOG_INFO("\n(%3u) ", ++line);
    } else if (*text == '\r') {
      // skip it to support Windows/UNIX EOLs
    } else {
      IGL_LOG_INFO("%c", *text);
    }
    text++;
  }
  IGL_LOG_INFO("\n");
#endif
#endif
}

} // namespace

namespace igl {
namespace vulkan {

VulkanShaderModule::VulkanShaderModule(const VulkanFunctionTable& vf,
                                       VkDevice device,
                                       VkShaderModule shaderModule,
                                       util::SpvModuleInfo&& moduleInfo) :
  vf_(vf), device_(device), vkShaderModule_(shaderModule), moduleInfo_(std::move(moduleInfo)) {}

VulkanShaderModule::~VulkanShaderModule() {
  vf_.vkDestroyShaderModule(device_, vkShaderModule_, nullptr);
}

} // namespace vulkan
} // namespace igl
