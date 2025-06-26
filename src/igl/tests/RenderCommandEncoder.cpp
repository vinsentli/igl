/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <cstddef>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <utility>
#include <vector>

#include "data/ShaderData.h"
#include "data/TextureData.h"
#include "util/Common.h"

#include <igl/Buffer.h>
#include <igl/CommandBuffer.h>
#include <igl/DepthStencilState.h>
#include <igl/NameHandle.h>
#include <igl/RenderCommandEncoder.h>
#include <igl/RenderPass.h>
#include <igl/RenderPipelineState.h>
#include <igl/SamplerState.h>
#include <igl/VertexInputState.h>

#define OFFSCREEN_RT_WIDTH 4
#define OFFSCREEN_RT_HEIGHT 4

#define OFFSCREEN_TEX_WIDTH 4
#define OFFSCREEN_TEX_HEIGHT 4

namespace igl::tests {

const auto kQuarterPixel = (float)(0.5 / OFFSCREEN_RT_WIDTH);
const float kBackgroundColor = 0.501f;
const uint32_t kBackgroundColorHex = 0x80808080;

/**
 * @brief RenderCommandEncoderTest is a test fixture for all the tests in this file.
 * It takes care of common initialization and allocating of common resources.
 */
class RenderCommandEncoderTest : public ::testing::Test {
 private:
 public:
  RenderCommandEncoderTest() = default;
  ~RenderCommandEncoderTest() override = default;

  /**
   * @brief This function sets up a render pass and a render pipeline descriptor
   * so it is ready to render a simple quad with an input texture to an
   * offscreen texture.
   */
  void SetUp() override {
    setDebugBreakEnabled(false);

    util::createDeviceAndQueue(iglDev_, cmdQueue_);
    ASSERT_TRUE(iglDev_ != nullptr);
    ASSERT_TRUE(cmdQueue_ != nullptr);

    // Create an offscreen texture to render to
    TextureDesc texDesc = TextureDesc::new2D(TextureFormat::RGBA_UNorm8,
                                             OFFSCREEN_RT_WIDTH,
                                             OFFSCREEN_RT_HEIGHT,
                                             TextureDesc::TextureUsageBits::Sampled |
                                                 TextureDesc::TextureUsageBits::Attachment);

    auto depthFormat = TextureFormat::S8_UInt_Z32_UNorm;
    if (backend_ == util::BACKEND_VUL) {
      depthFormat = TextureFormat::S8_UInt_Z24_UNorm;
    }

    TextureDesc depthTexDesc = TextureDesc::new2D(depthFormat,
                                                  OFFSCREEN_RT_WIDTH,
                                                  OFFSCREEN_RT_HEIGHT,
                                                  TextureDesc::TextureUsageBits::Sampled |
                                                      TextureDesc::TextureUsageBits::Attachment);
    depthTexDesc.storage = ResourceStorage::Private;

    Result ret;
    offscreenTexture_ = iglDev_->createTexture(texDesc, &ret);
    ASSERT_TRUE(ret.isOk()) << ret.message;
    ASSERT_TRUE(offscreenTexture_ != nullptr);

    depthStencilTexture_ = iglDev_->createTexture(depthTexDesc, &ret);
    ASSERT_TRUE(ret.isOk()) << ret.message;
    ASSERT_TRUE(depthStencilTexture_ != nullptr);

    // Create framebuffer using the offscreen texture
    FramebufferDesc framebufferDesc;

    framebufferDesc.colorAttachments[0].texture = offscreenTexture_;
    framebufferDesc.depthAttachment.texture = depthStencilTexture_;
    framebufferDesc.stencilAttachment.texture = depthStencilTexture_;

    framebuffer_ = iglDev_->createFramebuffer(framebufferDesc, &ret);
    ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
    ASSERT_TRUE(framebuffer_ != nullptr);

    // Initialize render pass descriptor
    renderPass_.colorAttachments.resize(1);
    renderPass_.colorAttachments[0].loadAction = LoadAction::Clear;
    renderPass_.colorAttachments[0].storeAction = StoreAction::Store;
    renderPass_.colorAttachments[0].clearColor = {
        kBackgroundColor, kBackgroundColor, kBackgroundColor, kBackgroundColor};

    renderPass_.depthAttachment.loadAction = LoadAction::Clear;
    renderPass_.depthAttachment.storeAction = StoreAction::Store;
    renderPass_.depthAttachment.clearDepth = 0.0;

    renderPass_.stencilAttachment.loadAction = LoadAction::Clear;
    renderPass_.stencilAttachment.storeAction = StoreAction::Store;
    renderPass_.stencilAttachment.clearStencil = 0;

    // Initialize shader stages
    std::unique_ptr<IShaderStages> stages;
    igl::tests::util::createSimpleShaderStages(iglDev_, stages);
    shaderStages_ = std::move(stages);

    // Initialize input to vertex shader
    VertexInputStateDesc inputDesc;

    inputDesc.attributes[0].format = VertexAttributeFormat::Float4;
    inputDesc.attributes[0].offset = 0;
    inputDesc.attributes[0].bufferIndex = data::shader::simplePosIndex;
    inputDesc.attributes[0].name = data::shader::simplePos;
    inputDesc.attributes[0].location = 0;
    inputDesc.inputBindings[0].stride = sizeof(float) * 4;

    inputDesc.attributes[1].format = VertexAttributeFormat::Float2;
    inputDesc.attributes[1].offset = 0;
    inputDesc.attributes[1].bufferIndex = data::shader::simpleUvIndex;
    inputDesc.attributes[1].name = data::shader::simpleUv;
    inputDesc.attributes[1].location = 1;
    inputDesc.inputBindings[1].stride = sizeof(float) * 2;

    // numAttributes has to equal to bindings when using more than 1 buffer
    inputDesc.numAttributes = inputDesc.numInputBindings = 2;

    vertexInputState_ = iglDev_->createVertexInputState(inputDesc, &ret);
    ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
    ASSERT_TRUE(vertexInputState_ != nullptr);

    // Initialize sampler state
    const SamplerStateDesc samplerDesc;
    samp_ = iglDev_->createSamplerState(samplerDesc, &ret);
    ASSERT_EQ(ret.code, Result::Code::Ok);
    ASSERT_TRUE(samp_ != nullptr);

    // Initialize Render Pipeline Descriptor, but leave the creation
    // to the individual tests in case further customization is required.
    // It cannot be `const` here as we mutate the desc later.
    RenderPipelineDesc renderPipelineDesc = {
        .vertexInputState = vertexInputState_,
        .shaderStages = shaderStages_,
        .targetDesc =
            {
                .colorAttachments = {{.textureFormat = offscreenTexture_->getFormat()}},
                .depthAttachmentFormat = depthStencilTexture_->getFormat(),
                .stencilAttachmentFormat = depthStencilTexture_->getFormat(),
            },
        .cullMode = igl::CullMode::Disabled,
        .fragmentUnitSamplerMap = {{textureUnit_, IGL_NAMEHANDLE(data::shader::simpleSampler)}},
    };

    texDesc = TextureDesc::new2D(TextureFormat::RGBA_UNorm8,
                                 OFFSCREEN_TEX_WIDTH,
                                 OFFSCREEN_TEX_HEIGHT,
                                 TextureDesc::TextureUsageBits::Sampled);
    texture_ = iglDev_->createTexture(texDesc, &ret);
    ASSERT_EQ(ret.code, Result::Code::Ok);
    ASSERT_TRUE(texture_ != nullptr);
    const auto rangeDesc = TextureRangeDesc::new2D(0, 0, OFFSCREEN_TEX_WIDTH, OFFSCREEN_TEX_HEIGHT);
    texture_->upload(rangeDesc, data::texture::TEX_RGBA_GRAY_4x4);

    auto createPipeline = [&renderPipelineDesc, &ret, this](
                              PrimitiveType topology) -> std::shared_ptr<IRenderPipelineState> {
      renderPipelineDesc.topology = topology;
      return iglDev_->createRenderPipeline(renderPipelineDesc, &ret);
    };
    renderPipelineStatePoint_ = createPipeline(PrimitiveType::Point);
    ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
    ASSERT_TRUE(renderPipelineStatePoint_ != nullptr);
    renderPipelineStateLine_ = createPipeline(PrimitiveType::Line);
    ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
    ASSERT_TRUE(renderPipelineStateLine_ != nullptr);
    renderPipelineStateLineStrip_ = createPipeline(PrimitiveType::LineStrip);
    ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
    ASSERT_TRUE(renderPipelineStateLineStrip_ != nullptr);
    renderPipelineStateTriangle_ = createPipeline(PrimitiveType::Triangle);
    ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
    ASSERT_TRUE(renderPipelineStateTriangle_ != nullptr);
    renderPipelineStateTriangleStrip_ = createPipeline(PrimitiveType::TriangleStrip);
    ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
    ASSERT_TRUE(renderPipelineStateTriangleStrip_ != nullptr);

    depthStencilState_ = iglDev_->createDepthStencilState({}, &ret);
    ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
    ASSERT_TRUE(depthStencilState_ != nullptr);

    bindGroupTexture_ = iglDev_->createBindGroup(
        BindGroupTextureDesc{{texture_}, {samp_}, "Offscreen texture test"}, nullptr, &ret);
    ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
  }

  void encodeAndSubmit(
      const std::function<void(const std::unique_ptr<IRenderCommandEncoder>&)>& func,
      bool useBindGroup = false,
      bool useNewBindTexture = false) {
    Result ret;

    auto cmdBuffer = cmdQueue_->createCommandBuffer({}, &ret);
    ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
    ASSERT_TRUE(cmdBuffer != nullptr);

    auto encoder = cmdBuffer->createRenderCommandEncoder(renderPass_, framebuffer_);

    if (useBindGroup) {
      encoder->bindBindGroup(bindGroupTexture_);
    } else {
      useNewBindTexture ? encoder->bindTexture(textureUnit_, texture_.get())
                        : encoder->bindTexture(textureUnit_, BindTarget::kFragment, texture_.get());
      encoder->bindSamplerState(textureUnit_, BindTarget::kFragment, samp_.get());
    }

    encoder->bindVertexBuffer(data::shader::simplePosIndex, *vb_);
    encoder->bindVertexBuffer(data::shader::simpleUvIndex, *uv_);

    encoder->bindDepthStencilState(depthStencilState_);

    if (ib_) {
      encoder->bindIndexBuffer(*ib_, IndexFormat::UInt32);
    }

    const igl::Viewport viewport = {
        0.0f, 0.0f, (float)OFFSCREEN_RT_WIDTH, (float)OFFSCREEN_RT_HEIGHT, 0.0f, +1.0f};
    const igl::ScissorRect scissor = {
        0, 0, (uint32_t)OFFSCREEN_RT_WIDTH, (uint32_t)OFFSCREEN_RT_HEIGHT};
    encoder->bindViewport(viewport);
    encoder->bindScissorRect(scissor);

    func(encoder);

    encoder->endEncoding();

    cmdQueue_->submit(*cmdBuffer);
    cmdBuffer->waitUntilCompleted();
  }

  void verifyFrameBuffer(const std::vector<uint32_t>& expectedPixels) {
    auto pixels =
        std::vector<uint32_t>(static_cast<size_t>(OFFSCREEN_RT_WIDTH * OFFSCREEN_RT_WIDTH));
    framebuffer_->copyBytesColorAttachment(
        *cmdQueue_,
        0,
        pixels.data(),
        TextureRangeDesc::new2D(0, 0, OFFSCREEN_RT_WIDTH, OFFSCREEN_RT_HEIGHT));

#if IGL_DEBUG
    debugLog(pixels);
#endif // IGL_DEBUG

    for (int i = 0; i < OFFSCREEN_RT_HEIGHT; i++) {
      for (int j = 0; j < OFFSCREEN_RT_WIDTH; j++) {
        ASSERT_EQ(pixels[(OFFSCREEN_RT_HEIGHT - i - 1) * OFFSCREEN_RT_WIDTH + j],
                  expectedPixels[i * OFFSCREEN_RT_WIDTH + j]);
      }
    }
  }

  void verifyFrameBuffer(const std::function<void(const std::vector<uint32_t>&)>& func) {
    auto pixels =
        std::vector<uint32_t>(static_cast<size_t>(OFFSCREEN_RT_WIDTH * OFFSCREEN_RT_WIDTH));
    framebuffer_->copyBytesColorAttachment(
        *cmdQueue_,
        0,
        pixels.data(),
        TextureRangeDesc::new2D(0, 0, OFFSCREEN_RT_WIDTH, OFFSCREEN_RT_HEIGHT));

#if IGL_DEBUG
    debugLog(pixels);
#endif // IGL_DEBUG

    func(pixels);
  }

  void debugLog(const std::vector<uint32_t>& pixels) {
    IGL_LOG_DEBUG("\nFrameBuffer begins.\n");
    IGL_LOG_DEBUG("%s\n", ::testing::UnitTest::GetInstance()->current_test_info()->name());
    for (int i = OFFSCREEN_RT_HEIGHT - 1; i >= 0; i--) {
      for (int j = 0; j < OFFSCREEN_RT_WIDTH; j++) {
        IGL_LOG_DEBUG("%x, ", pixels[i * OFFSCREEN_RT_WIDTH + j]);
      }
      IGL_LOG_DEBUG("\n");
    }
    IGL_LOG_DEBUG("\nFrameBuffer ends.\n");
  }

  void initializeBuffers(const std::vector<float>& verts,
                         const std::vector<float>& uvs,
                         const std::vector<uint32_t>& indices = {}) {
    BufferDesc bufDesc;

    bufDesc.type = BufferDesc::BufferTypeBits::Vertex;
    bufDesc.data = verts.data();
    bufDesc.length = sizeof(float) * verts.size();

    Result ret;
    vb_ = iglDev_->createBuffer(bufDesc, &ret);
    ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
    ASSERT_TRUE(vb_ != nullptr);

    bufDesc.type = BufferDesc::BufferTypeBits::Vertex;
    bufDesc.data = uvs.data();
    bufDesc.length = sizeof(float) * uvs.size();

    uv_ = iglDev_->createBuffer(bufDesc, &ret);
    ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
    ASSERT_TRUE(uv_ != nullptr);

    if (!indices.empty()) {
      bufDesc.type = BufferDesc::BufferTypeBits::Index;
      bufDesc.data = indices.data();
      bufDesc.length = sizeof(uint32_t) * indices.size();
      ib_ = iglDev_->createBuffer(bufDesc, &ret);
      ASSERT_TRUE(ret.isOk());
      ASSERT_TRUE(ib_ != nullptr);
    }
  }
  void initialize8BitIndices(const std::vector<uint8_t>& indices) {
    BufferDesc desc;
    desc.type = BufferDesc::BufferTypeBits::Index;
    desc.data = indices.data();
    desc.length = sizeof(uint8_t) * indices.size();
    Result ret;
    ib_ = iglDev_->createBuffer(desc, &ret);
    ASSERT_TRUE(ret.isOk());
    ASSERT_TRUE(ib_ != nullptr);
  }

  void TearDown() override {}

 protected:
  std::shared_ptr<IDevice> iglDev_;
  std::shared_ptr<ICommandQueue> cmdQueue_;

  RenderPassDesc renderPass_;
  std::shared_ptr<ITexture> offscreenTexture_;
  std::shared_ptr<ITexture> depthStencilTexture_;

  std::shared_ptr<IFramebuffer> framebuffer_;

  std::shared_ptr<IShaderStages> shaderStages_;

  std::shared_ptr<IVertexInputState> vertexInputState_;
  std::shared_ptr<IBuffer> vb_, uv_, ib_;

  std::shared_ptr<ISamplerState> samp_;

  std::shared_ptr<ITexture> texture_;

  std::shared_ptr<IRenderPipelineState> renderPipelineStatePoint_;
  std::shared_ptr<IRenderPipelineState> renderPipelineStateLine_;
  std::shared_ptr<IRenderPipelineState> renderPipelineStateLineStrip_;
  std::shared_ptr<IRenderPipelineState> renderPipelineStateTriangle_;
  std::shared_ptr<IRenderPipelineState> renderPipelineStateTriangleStrip_;
  std::shared_ptr<IDepthStencilState> depthStencilState_;
  Holder<BindGroupTextureHandle> bindGroupTexture_;

  const std::string backend_ = IGL_BACKEND_TYPE;

  size_t textureUnit_ = 0;
}; // namespace igl::tests

TEST_F(RenderCommandEncoderTest, shouldDrawAPoint) {
  initializeBuffers(
      // clang-format off
      { kQuarterPixel, kQuarterPixel, 0.0f, 1.0f },
      { 0.5, 0.5 } // clang-format on
  );

  encodeAndSubmit([this](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
    encoder->bindRenderPipelineState(renderPipelineStatePoint_);
    encoder->draw(1);
  });

  auto grayColor = data::texture::TEX_RGBA_GRAY_4x4[0];
  // clang-format off
  std::vector<uint32_t> const expectedPixels {
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, grayColor,          kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
  };
  // clang-format on

  verifyFrameBuffer(expectedPixels);
}

TEST_F(RenderCommandEncoderTest, shouldDrawAPointNewBindTexture) {
  initializeBuffers(
      // clang-format off
      { kQuarterPixel, kQuarterPixel, 0.0f, 1.0f },
      { 0.5, 0.5 } // clang-format on
  );

  encodeAndSubmit(
      [this](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
        encoder->bindRenderPipelineState(renderPipelineStatePoint_);
        encoder->draw(1);
      },
      false,
      true);

  auto grayColor = data::texture::TEX_RGBA_GRAY_4x4[0];
  // clang-format off
  std::vector<uint32_t> const expectedPixels {
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, grayColor,          kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
  };
  // clang-format on

  verifyFrameBuffer(expectedPixels);
}

TEST_F(RenderCommandEncoderTest, shouldDrawALine) {
  initializeBuffers(
      // clang-format off
      {
        -1.0f - kQuarterPixel, -1.0f + kQuarterPixel, 0.0f, 1.0f,
         1.0f + kQuarterPixel, -1.0f + kQuarterPixel, 0.0f, 1.0f,
      },
      {
        0.0f, 0.0f,
        1.0f, 0.0f,
      } // clang-format on
  );

  encodeAndSubmit([this](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
    encoder->bindRenderPipelineState(renderPipelineStateLine_);
    encoder->draw(2);
  });

  auto grayColor = data::texture::TEX_RGBA_GRAY_4x4[0];
  // clang-format off
  std::vector<uint32_t> const expectedPixels {
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
    grayColor,          grayColor,          grayColor,          grayColor,
  };
  // clang-format on

  verifyFrameBuffer(expectedPixels);
}

TEST_F(RenderCommandEncoderTest, shouldDrawLineStrip) {
  initializeBuffers(
      // clang-format off
      {
        -1.0f - kQuarterPixel, -1.0f + kQuarterPixel, 0.0f, 1.0f,
         1.0f + kQuarterPixel, -1.0f + kQuarterPixel, 0.0f, 1.0f,
         1.0f - kQuarterPixel, -1.0f - kQuarterPixel, 0.0f, 1.0f,
         1.0f - kQuarterPixel,  1.0f + kQuarterPixel, 0.0f, 1.0f,
      },
      {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
      } // clang-format on
  );

  encodeAndSubmit([this](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
    encoder->bindRenderPipelineState(renderPipelineStateLineStrip_);
    encoder->draw(4);
  });

  auto grayColor = data::texture::TEX_RGBA_GRAY_4x4[0];
  // clang-format off
  std::vector<uint32_t> const expectedPixels {
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, grayColor,
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, grayColor,
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, grayColor,
    grayColor,          grayColor,          grayColor,          grayColor,
  };
  // clang-format on

  verifyFrameBuffer(expectedPixels);
}

TEST_F(RenderCommandEncoderTest, drawIndexedFirstIndex) {
  if (!iglDev_->hasFeature(igl::DeviceFeatures::DrawFirstIndexFirstVertex)) {
    GTEST_SKIP();
    return;
  }
  initializeBuffers(
      // clang-format off
      {
        -1.0f - kQuarterPixel, -1.0f,                0.0f, 1.0f,
         1.0f,                -1.0f,                0.0f, 1.0f,
         1.0f,                 1.0f + kQuarterPixel, 0.0f, 1.0f,
      },
      {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
      },
      {
         0, 0, 0, 0, 1, 2, // the first 3 indices are dummies
      } // clang-format on
  );

  ASSERT_TRUE(ib_ != nullptr);

  encodeAndSubmit([this](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
    encoder->bindRenderPipelineState(renderPipelineStateTriangle_);
    encoder->drawIndexed(3, 1, 3); // skip the first 3 dummy indices
  });

  const auto grayColor = data::texture::TEX_RGBA_GRAY_4x4[0];
  // clang-format off
  const std::vector<uint32_t> expectedPixels {
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, grayColor,
    kBackgroundColorHex, kBackgroundColorHex, grayColor,          grayColor,
    kBackgroundColorHex, grayColor,          grayColor,          grayColor,
    grayColor,          grayColor,          grayColor,          grayColor,
  };
  // clang-format on

  verifyFrameBuffer(expectedPixels);
}

TEST_F(RenderCommandEncoderTest, drawIndexed8Bit) {
  if (!iglDev_->hasFeature(igl::DeviceFeatures::Indices8Bit)) {
    GTEST_SKIP();
    return;
  }
  initializeBuffers(
      // clang-format off
      {
        -1.0f - kQuarterPixel, -1.0f,                0.0f, 1.0f,
         1.0f,                -1.0f,                0.0f, 1.0f,
         1.0f,                 1.0f + kQuarterPixel, 0.0f, 1.0f,
      },
      {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
      } // clang-format on
  );
  initialize8BitIndices({0, 1, 2});

  ASSERT_TRUE(ib_ != nullptr);

  encodeAndSubmit([this](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
    encoder->bindRenderPipelineState(renderPipelineStateTriangle_);
    encoder->bindIndexBuffer(*ib_, IndexFormat::UInt8);
    encoder->drawIndexed(3);
  });

  const auto grayColor = data::texture::TEX_RGBA_GRAY_4x4[0];
  // clang-format off
  const std::vector<uint32_t> expectedPixels {
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, grayColor,
    kBackgroundColorHex, kBackgroundColorHex, grayColor,          grayColor,
    kBackgroundColorHex, grayColor,          grayColor,          grayColor,
    grayColor,          grayColor,          grayColor,          grayColor,
  };
  // clang-format on

  verifyFrameBuffer(expectedPixels);
}

TEST_F(RenderCommandEncoderTest, drawInstanced) {
  if (!iglDev_->hasFeature(igl::DeviceFeatures::DrawFirstIndexFirstVertex)) {
    GTEST_SKIP();
    return;
  }
  initializeBuffers(
      // clang-format off
      {
        -1.0f - kQuarterPixel, -1.0f,                0.0f, 1.0f,
         1.0f,                -1.0f,                0.0f, 1.0f,
         1.0f,                 1.0f + kQuarterPixel, 0.0f, 1.0f,
      },
      {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
      },
      {
         0, 1, 2,
      } // clang-format on
  );

  ASSERT_TRUE(ib_ != nullptr);

  encodeAndSubmit([this](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
    encoder->bindRenderPipelineState(renderPipelineStateTriangle_);
    // draw 2 indentical instances, one on top of another; this will trigger drawElementsInstanced()
    // in OpenGL
    encoder->drawIndexed(3, 2);
  });

  const auto grayColor = data::texture::TEX_RGBA_GRAY_4x4[0];
  // clang-format off
  const std::vector<uint32_t> expectedPixels {
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, grayColor,
    kBackgroundColorHex, kBackgroundColorHex, grayColor,          grayColor,
    kBackgroundColorHex, grayColor,          grayColor,          grayColor,
    grayColor,          grayColor,          grayColor,          grayColor,
  };
  // clang-format on

  verifyFrameBuffer(expectedPixels);
}

TEST_F(RenderCommandEncoderTest, shouldDrawATriangle) {
  initializeBuffers(
      // clang-format off
      {
        -1.0f - kQuarterPixel, -1.0f,                0.0f, 1.0f,
         1.0f,                -1.0f,                0.0f, 1.0f,
         1.0f,                 1.0f + kQuarterPixel, 0.0f, 1.0f,
      },
      {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
      } // clang-format on
  );

  encodeAndSubmit([this](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
    encoder->bindRenderPipelineState(renderPipelineStateTriangle_);
    encoder->draw(3);
  });

  auto grayColor = data::texture::TEX_RGBA_GRAY_4x4[0];
  // clang-format off
  std::vector<uint32_t> const expectedPixels {
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, grayColor,
    kBackgroundColorHex, kBackgroundColorHex, grayColor,          grayColor,
    kBackgroundColorHex, grayColor,          grayColor,          grayColor,
    grayColor,          grayColor,          grayColor,          grayColor,
  };

  verifyFrameBuffer(expectedPixels);
}

TEST_F(RenderCommandEncoderTest, shouldDrawTriangleStrip) {
  initializeBuffers(
      // clang-format off
      {
        -1.0f,  1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f,
      },
      {
        0.0, 1.0,
        0.0, 0.0,
        1.0, 1.0,
        1.0, 0.0,
      } // clang-format on
  );

  encodeAndSubmit([this](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
    encoder->insertDebugEventLabel("Rendering a triangle strip...");
    encoder->bindRenderPipelineState(renderPipelineStateTriangleStrip_);
    encoder->draw(4);
  });

  verifyFrameBuffer([](const std::vector<uint32_t>& pixels) {
    for (const auto& pixel : pixels) {
      ASSERT_EQ(pixel, data::texture::TEX_RGBA_GRAY_4x4[0]);
    }
  });
}

TEST_F(RenderCommandEncoderTest, shouldDrawTriangleStripCopyTextureToBuffer) {
  if (iglDev_->getBackendType() != igl::BackendType::Vulkan) {
    GTEST_SKIP() << "Not implemented for non-Vulkan backends";
    return;
  }

  initializeBuffers(
      // clang-format off
      {
        -1.0f,  1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f,
      },
      {
        0.0, 1.0,
        0.0, 0.0,
        1.0, 1.0,
        1.0, 0.0,
      } // clang-format on
  );

  Result ret;

  std ::shared_ptr<IBuffer> screenCopy =
      iglDev_->createBuffer(BufferDesc(BufferDesc::BufferTypeBits::Storage,
                                       nullptr,
                                       OFFSCREEN_RT_WIDTH * OFFSCREEN_RT_HEIGHT * sizeof(uint32_t),
                                       ResourceStorage::Shared,
                                       0,
                                       "Buffer: screen copy"),
                            &ret);
  ASSERT_TRUE(ret.isOk()) << ret.message.c_str();

  auto cmdBuffer = cmdQueue_->createCommandBuffer({}, &ret);
  ASSERT_TRUE(ret.isOk()) << ret.message.c_str();
  ASSERT_TRUE(cmdBuffer != nullptr);

  auto encoder = cmdBuffer->createRenderCommandEncoder(renderPass_, framebuffer_);

  encoder->bindTexture(textureUnit_, texture_.get());
  encoder->bindSamplerState(textureUnit_, BindTarget::kFragment, samp_.get());

  encoder->bindVertexBuffer(data::shader::simplePosIndex, *vb_);
  encoder->bindVertexBuffer(data::shader::simpleUvIndex, *uv_);

  encoder->bindDepthStencilState(depthStencilState_);

  encoder->bindViewport(
      {0.0f, 0.0f, (float)OFFSCREEN_RT_WIDTH, (float)OFFSCREEN_RT_HEIGHT, 0.0f, +1.0f});
  encoder->bindScissorRect({0, 0, (uint32_t)OFFSCREEN_RT_WIDTH, (uint32_t)OFFSCREEN_RT_HEIGHT});

  encoder->insertDebugEventLabel("Rendering a triangle strip...");
  encoder->bindRenderPipelineState(renderPipelineStateTriangleStrip_);
  encoder->draw(4);

  encoder->endEncoding();

  cmdBuffer->copyTextureToBuffer(*framebuffer_->getColorAttachment(0), *screenCopy, 0);

  cmdQueue_->submit(*cmdBuffer);
  cmdBuffer->waitUntilCompleted();

  const uint32_t* data = static_cast<const uint32_t*>(
      screenCopy->map(BufferRange(screenCopy->getSizeInBytes()), nullptr));
  for (size_t i = 0; i != OFFSCREEN_RT_HEIGHT * OFFSCREEN_RT_HEIGHT; i++) {
    ASSERT_EQ(data[i], data::texture::TEX_RGBA_GRAY_4x4[0]);
  }
  screenCopy->unmap();
}

TEST_F(RenderCommandEncoderTest, shouldNotDraw) {
  initializeBuffers(
      // clang-format off
      {
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
      },
      {
        0.0, 1.0,
        1.0, 1.0,
        0.0, 0.0,
        1.0, 1.0,
        1.0, 0.0,
        0.0, 0.0,
      } // clang-format on
  );

  encodeAndSubmit([this](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
    encoder->bindRenderPipelineState(renderPipelineStatePoint_);
    encoder->draw(0);
    encoder->bindRenderPipelineState(renderPipelineStateLine_);
    encoder->draw(0);
    encoder->bindRenderPipelineState(renderPipelineStateLineStrip_);
    encoder->draw(0);
    encoder->bindRenderPipelineState(renderPipelineStateTriangle_);
    encoder->draw(0);
    encoder->bindRenderPipelineState(renderPipelineStateTriangleStrip_);
    encoder->draw(0);
  });

  verifyFrameBuffer([](const std::vector<uint32_t>& pixels) {
    for (const auto& pixel : pixels) {
      ASSERT_EQ(pixel, kBackgroundColorHex);
    }
  });
}

TEST_F(RenderCommandEncoderTest, shouldDrawATriangleBindGroup) {
#if IGL_PLATFORM_APPLE
  if (iglDev_->getBackendType() == igl::BackendType::Vulkan) {
    // @fb-only
    GTEST_SKIP() << "Broken on macOS arm64";
    return;
  }
#endif

  initializeBuffers(
      // clang-format off
      {
        -1.0f - kQuarterPixel, -1.0f,                0.0f, 1.0f,
         1.0f,                -1.0f,                0.0f, 1.0f,
         1.0f,                 1.0f + kQuarterPixel, 0.0f, 1.0f,
      },
      {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
      } // clang-format on
  );

  encodeAndSubmit(
      [this](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
        encoder->insertDebugEventLabel("Rendering a triangle...");
        encoder->bindRenderPipelineState(renderPipelineStateTriangle_);
        encoder->draw(3);
      },
      true);

  auto grayColor = data::texture::TEX_RGBA_GRAY_4x4[0];
  // clang-format off
  std::vector<uint32_t> const expectedPixels {
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, grayColor,
    kBackgroundColorHex, kBackgroundColorHex, grayColor,          grayColor,
    kBackgroundColorHex, grayColor,          grayColor,          grayColor,
    grayColor,          grayColor,          grayColor,          grayColor,
  };

  verifyFrameBuffer(expectedPixels);
}

TEST_F(RenderCommandEncoderTest, DepthBiasShouldDrawAPoint) {
  initializeBuffers(
      // clang-format off
      { kQuarterPixel, kQuarterPixel, 0.0f, 1.0f },
      { 0.5, 0.5 } // clang-format on
  );

  encodeAndSubmit([this](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
    encoder->bindRenderPipelineState(renderPipelineStatePoint_);
    encoder->setDepthBias(0, 0, 0);
    encoder->draw(1);
  });

  auto grayColor = data::texture::TEX_RGBA_GRAY_4x4[0];
  // clang-format off
  std::vector<uint32_t> const expectedPixels {
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, grayColor,          kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
  };
  // clang-format on

  verifyFrameBuffer(expectedPixels);
}

TEST_F(RenderCommandEncoderTest, drawUsingBindPushConstants) {
  if (iglDev_->getBackendType() != igl::BackendType::Vulkan) {
    GTEST_SKIP() << "Push constants are only supported in Vulkan";
    return;
  }

  initializeBuffers(
      // clang-format off
      { kQuarterPixel, kQuarterPixel, 0.0f, 1.0f },
      { 0.5, 0.5 } // clang-format on
  );

  // Create new shader stages with push constant shaders
  std::unique_ptr<IShaderStages> pushConstantStages;
  igl::tests::util::createShaderStages(iglDev_,
                                       data::shader::VULKAN_PUSH_CONSTANT_VERT_SHADER,
                                       igl::tests::data::shader::shaderFunc,
                                       data::shader::VULKAN_PUSH_CONSTANT_FRAG_SHADER,
                                       igl::tests::data::shader::shaderFunc,
                                       pushConstantStages);
  ASSERT_TRUE(pushConstantStages);
  shaderStages_ = std::move(pushConstantStages);

  // Create pipeline with push constant shaders
  const RenderPipelineDesc pipelineDesc = {
      .topology = PrimitiveType::Point,
      .vertexInputState = vertexInputState_,
      .shaderStages = shaderStages_,
      .targetDesc = {.colorAttachments = {{.textureFormat = offscreenTexture_->getFormat()}},
                     .depthAttachmentFormat = depthStencilTexture_->getFormat(),
                     .stencilAttachmentFormat = depthStencilTexture_->getFormat()},
      .cullMode = igl::CullMode::Disabled,
      .fragmentUnitSamplerMap = {{textureUnit_, IGL_NAMEHANDLE(data::shader::simpleSampler)}},
  };

  Result ret;
  auto pipelineWithPushConstants = iglDev_->createRenderPipeline(pipelineDesc, &ret);
  ASSERT_TRUE(ret.isOk());

  // Color multiplied by 1.5
  const float pushData[] = {1.5f, 1.5f, 1.5f, 1.5f};

  encodeAndSubmit([&](const std::unique_ptr<IRenderCommandEncoder>& encoder) {
    encoder->bindRenderPipelineState(pipelineWithPushConstants);
    encoder->bindPushConstants(pushData, sizeof(pushData), 0);
    encoder->draw(1);
  });

  // Expect 0x888888FF (0x888888 * 1.5) in the center of the screen
  const uint32_t expectedColor = 0xCCCCCCFF;

  // clang-format off
  std::vector<uint32_t> const expectedPixels {
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, expectedColor,      kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
    kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex, kBackgroundColorHex,
  };
  // clang-format on

  verifyFrameBuffer(expectedPixels);
}

} // namespace igl::tests
