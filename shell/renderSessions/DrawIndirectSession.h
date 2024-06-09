/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// @fb-only

#pragma once

#include <igl/IGL.h>
#include <shell/shared/platform/Platform.h>
#include <shell/shared/renderSession/RenderSession.h>

namespace igl::shell {



class DrawIndirectSession : public RenderSession {
 public:
  explicit DrawIndirectSession(std::shared_ptr<Platform> platform) :
    RenderSession(std::move(platform)) {}
  void initialize() noexcept override;
  void update(igl::SurfaceTextures surfaceTextures) noexcept override;
    
    typedef struct {
        uint32_t vertexCount;
        uint32_t instanceCount;
        uint32_t vertexStart;
        uint32_t baseInstance;
    } DrawArraysIndirectCommand;
    
    typedef  struct {
            uint  count;
            uint  instanceCount;
            uint  firstIndex;
            int  baseVertex;
            uint  baseInstance;
        } DrawElementsIndirectCommand;

 private:
  std::shared_ptr<ICommandQueue> commandQueue_;
  RenderPassDesc renderPass_;
  std::shared_ptr<IFramebuffer> framebuffer_;
  std::shared_ptr<IRenderPipelineState> renderPipelineState_Triangle_;
  std::shared_ptr<IBuffer> vertex_buffer_;
  std::shared_ptr<IBuffer> index_buffer_;
  std::shared_ptr<IBuffer> DrawArraysIndirectCommand_buffer_;
  std::shared_ptr<IBuffer> DrawElementsIndirectCommand_buffer_;
};

} // namespace igl::shell
