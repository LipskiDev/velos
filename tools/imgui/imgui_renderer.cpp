#include "imgui_renderer.h"

#include "shader/shader_compiler.h"
#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace VRHI = Velos::RHI;

namespace {

struct UiPushConstants {
  float scale[2];
  float translate[2];
};

static Velos::u32 GrowCapacity(Velos::u32 current, Velos::u32 required) {
  if (current >= required)
    return current;

  Velos::u32 newCapacity = (current == 0) ? 1024 : current;
  while (newCapacity < required)
    newCapacity *= 2;
  return newCapacity;
}

static VRHI::PipelineHandle
CreateImGuiPipeline(VRHI::IDevice *device, VRHI::Format colorFormat,
                    VRHI::Format depthFormat,
                    VRHI::DescriptorSetLayoutHandle setLayout) {

  const auto vsOutput = Velos::ShaderCompiler::CompileFile({
      .path = "tools/imgui/shaders/imgui.vert",
      .stage = VRHI::ShaderStage::Vertex,
      .entryPoint = "main",
  });

  const auto fsOutput = Velos::ShaderCompiler::CompileFile({
      .path = "tools/imgui/shaders/imgui.frag",
      .stage = VRHI::ShaderStage::Fragment,
      .entryPoint = "main",
  });

  const auto vs = device->CreateShader({
      .stage = VRHI::ShaderStage::Vertex,
      .bytecode = vsOutput.spirv.data(),
      .bytecodeSize = vsOutput.spirv.size() * sizeof(uint32_t),
      .entryPoint = "main",
      .reflection = vsOutput.reflection,
  });

  const auto fs = device->CreateShader({
      .stage = VRHI::ShaderStage::Fragment,
      .bytecode = fsOutput.spirv.data(),
      .bytecodeSize = fsOutput.spirv.size() * sizeof(uint32_t),
      .entryPoint = "main",
      .reflection = fsOutput.reflection,
  });

  VRHI::VertexBufferLayoutDesc layout{};
  layout.stride = sizeof(ImDrawVert);
  layout.inputRate = VRHI::VertexInputRate::PerVertex;
  layout.attributes = {
      {0, 0, VRHI::VertexFormat::Float32x2,
       static_cast<Velos::u32>(offsetof(ImDrawVert, pos))},
      {1, 0, VRHI::VertexFormat::Float32x2,
       static_cast<Velos::u32>(offsetof(ImDrawVert, uv))},
      {2, 0, VRHI::VertexFormat::UNorm8x4,
       static_cast<Velos::u32>(offsetof(ImDrawVert, col))},
  };

  const VRHI::DescriptorSetLayoutHandle layouts[] = {setLayout};

  VRHI::GraphicsPipelineDesc desc{};
  desc.vertexShader = vs;
  desc.fragmentShader = fs;
  desc.vertexLayouts = {layout};
  desc.layout = {.descriptorSetLayouts = layouts,
                 .descriptorSetLayoutCount = 1};
  desc.topology = VRHI::PrimitiveTopology::TriangleList;
  desc.colorFormat = colorFormat;
  desc.debugName = "ImGui Pipeline";

  desc.raster.cullBackFaces = false;
  desc.raster.frontFaceCCW = true;
  desc.raster.wireframe = false;

  desc.depth.depthTestEnable = false;
  desc.depth.depthWriteEnable = false;
  desc.depth.depthFormat = depthFormat;

  desc.blend.enable = true;
  desc.blend.srcColor = VRHI::BlendFactor::SrcAlpha;
  desc.blend.dstColor = VRHI::BlendFactor::OneMinusSrcAlpha;
  desc.blend.colorOp = VRHI::BlendOp::Add;
  desc.blend.srcAlpha = VRHI::BlendFactor::One;
  desc.blend.dstAlpha = VRHI::BlendFactor::OneMinusSrcAlpha;
  desc.blend.alphaOp = VRHI::BlendOp::Add;

  auto pipeline = device->CreateGraphicsPipeline(desc);

  device->DestroyShader(vs);
  device->DestroyShader(fs);

  return pipeline;
}

} // namespace

void ImGuiRenderer::Initialize(VRHI::IDevice *device,
                               VRHI::SwapchainHandle swapchain,
                               VRHI::Format colorFormat,
                               VRHI::Format depthFormat) {
  if (!device) {
    throw std::runtime_error("ImGuiRenderer::Initialize: device is null");
  }

  device_ = device;

  ImGuiIO &io = ImGui::GetIO();

  unsigned char *pixels = nullptr;
  int w = 0;
  int h = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);

  if (!pixels || w <= 0 || h <= 0) {
    throw std::runtime_error(
        "ImGuiRenderer::Initialize: failed to get font atlas");
  }

  fontImage_ = device_->CreateImage({
      .width = static_cast<Velos::u32>(w),
      .height = static_cast<Velos::u32>(h),
      .depth = 1,
      .mipLevels = 1,
      .arrayLayers = 1,
      .format = VRHI::Format::RGBA8_UNORM,
      .usage = VRHI::ImageUsage::TransferDst | VRHI::ImageUsage::Sampled,
      .debugName = "ImGui Font Image",
  });

  fontImageView_ = device_->CreateImageView({
      .image = fontImage_,
      .format = VRHI::Format::RGBA8_UNORM,
      .aspect = VRHI::ImageAspect::Color,
      .baseMipLevel = 0,
      .mipLevelCount = 1,
      .baseArrayLayer = 0,
      .arrayLayerCount = 1,
      .debugName = "ImGui Font Image View",
  });

  VRHI::SamplerDesc samplerDesc{};
  samplerDesc.minFilter = VRHI::Filter::Linear;
  samplerDesc.magFilter = VRHI::Filter::Linear;
  samplerDesc.addressU = VRHI::SamplerAddressMode::ClampToEdge;
  samplerDesc.addressV = VRHI::SamplerAddressMode::ClampToEdge;
  samplerDesc.addressW = VRHI::SamplerAddressMode::ClampToEdge;
  samplerDesc.enableAnisotropy = false;
  samplerDesc.maxAnisotropy = 1.0f;
  samplerDesc.debugName = "ImGui Font Sampler";
  fontSampler_ = device_->CreateSampler(samplerDesc);

  auto staging = device_->CreateBuffer({
      .size = static_cast<Velos::u64>(w) * static_cast<Velos::u64>(h) * 4ull,
      .usage = VRHI::BufferUsage::TransferSrc,
      .memoryUsage = VRHI::MemoryUsage::CPUToGPU,
      .initialData = pixels,
      .debugName = "ImGui Font Staging Buffer",
  });

  VRHI::BufferImageCopyRegion region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.mipLevel = 0;
  region.baseArrayLayer = 0;
  region.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {static_cast<Velos::u32>(w), static_cast<Velos::u32>(h),
                        1};
  region.aspect = VRHI::ImageAspect::Color;

  auto &cmd = device_->GetCommandList(VRHI::CommandListHandle{1});

  cmd.Begin();

  cmd.Barrier(
      {fontImage_, VRHI::ImageLayout::TransferDst, VRHI::ImageAspect::Color});
  cmd.CopyBufferToImage(staging, fontImage_, region);
  cmd.Barrier({fontImage_, VRHI::ImageLayout::ShaderReadOnly,
               VRHI::ImageAspect::Color});

  cmd.End();

  device_->Submit(VRHI::CommandListHandle{1});
  device_->WaitIdle();
  device_->DestroyBuffer(staging);

  VRHI::DescriptorBindingDesc binding{};
  binding.binding = 0;
  binding.type = VRHI::DescriptorType::CombinedImageSampler;
  binding.count = 1;
  binding.visibility = VRHI::ShaderStage::Fragment;

  VRHI::DescriptorSetLayoutDesc setLayoutDesc{};
  setLayoutDesc.bindings = &binding;
  setLayoutDesc.bindingCount = 1;
  setLayoutDesc.debugName = "ImGui Descriptor Set Layout";

  setLayout_ = device_->CreateDescriptorSetLayout(setLayoutDesc);

  VRHI::DescriptorPoolSize poolSize{};
  poolSize.type = VRHI::DescriptorType::CombinedImageSampler;
  poolSize.count = 1;

  VRHI::DescriptorPoolDesc poolDesc{};
  poolDesc.poolSizes = &poolSize;
  poolDesc.poolSizeCount = 1;
  poolDesc.maxSets = 1;
  poolDesc.debugName = "ImGui Descriptor Pool";

  pool_ = device_->CreateDescriptorPool(poolDesc);
  fontSet_ = device_->AllocateDescriptorSet(pool_, setLayout_,
                                            "ImGui Font Descriptor Set");

  VRHI::DescriptorImageInfo info{};
  info.sampler = fontSampler_;
  info.imageView = fontImageView_;
  info.imageLayout = VRHI::ImageLayout::ShaderReadOnly;

  VRHI::WriteDescriptorDesc write{};
  write.dstSet = fontSet_;
  write.binding = 0;
  write.arrayElement = 0;
  write.type = VRHI::DescriptorType::CombinedImageSampler;
  write.bufferInfo = nullptr;
  write.imageInfo = &info;
  write.descriptorCount = 1;

  device_->UpdateDescriptorSet(write);

  io.Fonts->SetTexID(static_cast<ImTextureID>(static_cast<uintptr_t>(1)));

  pipeline_ =
      CreateImGuiPipeline(device_, colorFormat, depthFormat, setLayout_);
}

void ImGuiRenderer::Shutdown() {
  if (!device_) {
    return;
  }

  device_->WaitIdle();

  if (frame_.vertexCapacityBytes > 0) {
    device_->DestroyBuffer(frame_.vertexBuffer);
    frame_.vertexBuffer = {};
    frame_.vertexCapacityBytes = 0;
  }

  if (frame_.indexCapacityBytes > 0) {
    device_->DestroyBuffer(frame_.indexBuffer);
    frame_.indexBuffer = {};
    frame_.indexCapacityBytes = 0;
  }

  if (pipeline_.IsValid()) {
    device_->DestroyPipeline(pipeline_);
    pipeline_ = {};
  }

  if (pool_.IsValid()) {
    device_->DestroyDescriptorPool(pool_);
    pool_ = {};
  }

  if (setLayout_.IsValid()) {
    device_->DestroyDescriptorSetLayout(setLayout_);
    setLayout_ = {};
  }

  if (fontSampler_.IsValid()) {
    device_->DestroySampler(fontSampler_);
    fontSampler_ = {};
  }

  if (fontImageView_.IsValid()) {
    device_->DestroyImageView(fontImageView_);
    fontImageView_ = {};
  }

  if (fontImage_.IsValid()) {
    device_->DestroyImage(fontImage_);
    fontImage_ = {};
  }

  fontSet_ = {};
  device_ = nullptr;
}

void ImGuiRenderer::NewFrame(float deltaSeconds, int windowWidth,
                             int windowHeight, int framebufferWidth,
                             int framebufferHeight) {
  ImGuiIO &io = ImGui::GetIO();

  io.DeltaTime = deltaSeconds > 0.0f ? deltaSeconds : (1.0f / 60.0f);

  io.DisplaySize =
      ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight));

  io.DisplayFramebufferScale =
      ImVec2(windowWidth > 0 ? static_cast<float>(framebufferWidth) /
                                   static_cast<float>(windowWidth)
                             : 1.0f,
             windowHeight > 0 ? static_cast<float>(framebufferHeight) /
                                    static_cast<float>(windowHeight)
                              : 1.0f);

  ImGui::NewFrame();
}

void ImGuiRenderer::Render(VRHI::ICommandList &cmd, ImDrawData *drawData) {
  if (!device_ || !drawData || drawData->TotalVtxCount <= 0 ||
      drawData->TotalIdxCount <= 0) {
    return;
  }

  auto &frame = frame_;

  const Velos::u32 vbSize = static_cast<Velos::u32>(
      drawData->TotalVtxCount * static_cast<int>(sizeof(ImDrawVert)));
  const Velos::u32 ibSize = static_cast<Velos::u32>(
      drawData->TotalIdxCount * static_cast<int>(sizeof(ImDrawIdx)));

  const Velos::u32 newVBSize = GrowCapacity(frame.vertexCapacityBytes, vbSize);
  const Velos::u32 newIBSize = GrowCapacity(frame.indexCapacityBytes, ibSize);

  if (newVBSize != frame.vertexCapacityBytes) {
    if (frame.vertexCapacityBytes > 0) {
      device_->DestroyBuffer(frame.vertexBuffer);
    }

    frame.vertexBuffer = device_->CreateBuffer({
        .size = newVBSize,
        .usage = VRHI::BufferUsage::Vertex,
        .memoryUsage = VRHI::MemoryUsage::CPUToGPU,
        .initialData = nullptr,
        .debugName = "ImGui Vertex Buffer",
    });

    frame.vertexCapacityBytes = newVBSize;
  }

  if (newIBSize != frame.indexCapacityBytes) {
    if (frame.indexCapacityBytes > 0) {
      device_->DestroyBuffer(frame.indexBuffer);
    }

    frame.indexBuffer = device_->CreateBuffer({
        .size = newIBSize,
        .usage = VRHI::BufferUsage::Index,
        .memoryUsage = VRHI::MemoryUsage::CPUToGPU,
        .initialData = nullptr,
        .debugName = "ImGui Index Buffer",
    });

    frame.indexCapacityBytes = newIBSize;
  }

  std::vector<ImDrawVert> vtx;
  std::vector<ImDrawIdx> idx;
  vtx.reserve(drawData->TotalVtxCount);
  idx.reserve(drawData->TotalIdxCount);

  for (int i = 0; i < drawData->CmdListsCount; i++) {
    auto *list = drawData->CmdLists[i];
    vtx.insert(vtx.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
    idx.insert(idx.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
  }

  VRHI::BufferUpdateDesc vbUpdate{};
  vbUpdate.buffer = frame.vertexBuffer;
  vbUpdate.offset = 0;
  vbUpdate.data = vtx.data();
  vbUpdate.size = static_cast<Velos::u64>(vtx.size() * sizeof(ImDrawVert));

  VRHI::BufferUpdateDesc ibUpdate{};
  ibUpdate.buffer = frame.indexBuffer;
  ibUpdate.offset = 0;
  ibUpdate.data = idx.data();
  ibUpdate.size = static_cast<Velos::u64>(idx.size() * sizeof(ImDrawIdx));

  cmd.UpdateBuffer(vbUpdate);
  cmd.UpdateBuffer(ibUpdate);

  cmd.BindPipeline(pipeline_);
  cmd.BindDescriptorSet(pipeline_, 0, fontSet_);
  cmd.SetViewport(VRHI::Viewport{
      .x = 0.0f,
      .y = 0.0f,
      .width = drawData->DisplaySize.x * drawData->FramebufferScale.x,
      .height = drawData->DisplaySize.y * drawData->FramebufferScale.y,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  });

  cmd.BindVertexBuffer(0, frame.vertexBuffer, 0);
  cmd.BindIndexBuffer(
      frame.indexBuffer,
      sizeof(ImDrawIdx) == 2 ? VRHI::IndexType::U16 : VRHI::IndexType::U32, 0);

  const float displayW = drawData->DisplaySize.x;
  const float displayH = drawData->DisplaySize.y;

  UiPushConstants push{};
  push.scale[0] = 2.0f / displayW;
  push.scale[1] = 2.0f / displayH;
  push.translate[0] = -1.0f - drawData->DisplayPos.x * push.scale[0];
  push.translate[1] = -1.0f - drawData->DisplayPos.y * push.scale[1];

  cmd.PushConstants(VRHI::ShaderStage::Vertex, 0, sizeof(UiPushConstants),
                    &push);

  int globalVtxOffset = 0;
  int globalIdxOffset = 0;

  for (int n = 0; n < drawData->CmdListsCount; n++) {
    const ImDrawList *cmdList = drawData->CmdLists[n];

    for (int cmdIndex = 0; cmdIndex < cmdList->CmdBuffer.Size; cmdIndex++) {
      const ImDrawCmd *pcmd = &cmdList->CmdBuffer[cmdIndex];

      if (pcmd->UserCallback != nullptr) {
        if (pcmd->UserCallback == ImDrawCallback_ResetRenderState) {
          cmd.BindPipeline(pipeline_);
          cmd.BindDescriptorSet(pipeline_, 0, fontSet_);
          cmd.BindVertexBuffer(0, frame.vertexBuffer, 0);
          cmd.BindIndexBuffer(frame.indexBuffer,
                              sizeof(ImDrawIdx) == 2 ? VRHI::IndexType::U16
                                                     : VRHI::IndexType::U32,
                              0);
          cmd.PushConstants(VRHI::ShaderStage::Vertex, 0,
                            sizeof(UiPushConstants), &push);
        } else {
          pcmd->UserCallback(cmdList, pcmd);
        }
        continue;
      }

      ImVec2 clipMin((pcmd->ClipRect.x - drawData->DisplayPos.x) *
                         drawData->FramebufferScale.x,
                     (pcmd->ClipRect.y - drawData->DisplayPos.y) *
                         drawData->FramebufferScale.y);

      ImVec2 clipMax((pcmd->ClipRect.z - drawData->DisplayPos.x) *
                         drawData->FramebufferScale.x,
                     (pcmd->ClipRect.w - drawData->DisplayPos.y) *
                         drawData->FramebufferScale.y);

      if (clipMin.x < 0.0f)
        clipMin.x = 0.0f;
      if (clipMin.y < 0.0f)
        clipMin.y = 0.0f;
      if (clipMax.x > drawData->DisplaySize.x * drawData->FramebufferScale.x)
        clipMax.x = drawData->DisplaySize.x * drawData->FramebufferScale.x;
      if (clipMax.y > drawData->DisplaySize.y * drawData->FramebufferScale.y)
        clipMax.y = drawData->DisplaySize.y * drawData->FramebufferScale.y;

      if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) {
        continue;
      }

      VRHI::Rect2D scissor{};
      scissor.offset = {static_cast<Velos::i32>(clipMin.x),
                        static_cast<Velos::i32>(clipMin.y)};
      scissor.extent = {
          static_cast<Velos::u32>(clipMax.x - clipMin.x),
          static_cast<Velos::u32>(clipMax.y - clipMin.y),
      };

      cmd.SetScissor(scissor);

      cmd.DrawIndexed(
          static_cast<Velos::u32>(pcmd->ElemCount),
          static_cast<Velos::u32>(pcmd->IdxOffset + globalIdxOffset),
          static_cast<Velos::i32>(pcmd->VtxOffset + globalVtxOffset));
    }

    globalIdxOffset += cmdList->IdxBuffer.Size;
    globalVtxOffset += cmdList->VtxBuffer.Size;
  }
}

void ImGuiRenderer::OnResize(int fbWidth, int fbHeight) {
  (void)fbWidth;
  (void)fbHeight;
}
