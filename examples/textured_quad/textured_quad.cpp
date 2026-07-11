#include "core/window.h"
#include "rhi/command_list.h"
#include "rhi/device.h"
#include "rhi/pipeline.h"
#include "rhi/types.h"
#include "shader/shader_compiler.h"

#include <core/application.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace Velos;

struct Vertex {
  glm::vec3 pos;
  glm::vec2 uv;
};

int main() {
  using namespace Velos::RHI;

  std::cout << "Creating app\n";
  Application app({
      .width = 1280,
      .height = 720,
      .title = "Velos Textured Quad",
      .resizable = true,
  });

  std::cout << "Creating device\n";
  IDevice *device = CreateDevice({
      .graphicsAPI = GraphicsAPI::Vulkan,
      .enableValidation = true,
      .applicationName = "Velos Textured Quad",
  });

  std::cout << "Creating swapchain\n";
  SwapchainHandle swapchain = device->CreateSwapchain({
      .windowHandle = app.GetWindow().GetNativeHandle(),
      .width = static_cast<u32>(app.GetWindow().GetFramebufferWidth()),
      .height = static_cast<u32>(app.GetWindow().GetFramebufferHeight()),
      .format = Format::RGBA8_UNORM,
      .bufferCount = 2,
      .vsync = true,
      .debugName = "Main Swapchain",
  });

  std::vector<Vertex> vertices = {
      {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}},
      {{0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}},
      {{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}},
      {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f}},
  };

  std::vector<std::uint16_t> indices = {
      0, 1, 2, 0, 2, 3,
  };

  std::cout << "Creating vertex buffer\n";
  BufferHandle vertexBuffer = device->CreateBuffer({
      .size = static_cast<u64>(vertices.size() * sizeof(Vertex)),
      .usage = BufferUsage::Vertex,
      .memoryUsage = MemoryUsage::CPUToGPU,
      .initialData = vertices.data(),
      .debugName = "Quad Vertex Buffer",
  });

  std::cout << "Creating index buffer\n";
  BufferHandle indexBuffer = device->CreateBuffer({
      .size = static_cast<u64>(indices.size() * sizeof(std::uint16_t)),
      .usage = BufferUsage::Index,
      .memoryUsage = MemoryUsage::CPUToGPU,
      .initialData = indices.data(),
      .debugName = "Quad Index Buffer",
  });

  std::cout << "Loading transparent texture with stb_image\n";
  stbi_set_flip_vertically_on_load(1);

  int texWidth = 0;
  int texHeight = 0;
  int texChannels = 0;
  stbi_uc *pixels =
      stbi_load("examples/textured_quad/awesomeface.png", &texWidth, &texHeight,
                &texChannels, STBI_rgb_alpha);

  if (!pixels) {
    throw std::runtime_error("Failed to load awesomeface.png with stb_image");
  }

  const u64 imageSize =
      static_cast<u64>(texWidth) * static_cast<u64>(texHeight) * 4ull;

  std::cout << "Creating staging buffer\n";
  BufferHandle stagingBuffer = device->CreateBuffer({
      .size = imageSize,
      .usage = BufferUsage::TransferSrc,
      .memoryUsage = MemoryUsage::CPUToGPU,
      .initialData = pixels,
      .debugName = "Texture Upload Staging Buffer",
  });

  stbi_image_free(pixels);

  std::cout << "Creating texture image\n";
  ImageHandle textureImage = device->CreateImage({
      .width = static_cast<u32>(texWidth),
      .height = static_cast<u32>(texHeight),
      .depth = 1,
      .mipLevels = 1,
      .arrayLayers = 1,
      .format = Format::RGBA8_UNORM,
      .usage = ImageUsage::TransferDst | ImageUsage::Sampled,
      .debugName = "Texture Image",
  });

  ImageViewHandle textureView = device->CreateImageView({
      .image = textureImage,
      .format = Format::RGBA8_UNORM,
      .aspect = ImageAspect::Color,
      .baseMipLevel = 0,
      .mipLevelCount = 1,
      .baseArrayLayer = 0,
      .arrayLayerCount = 1,
      .debugName = "Texture View",
  });

  SamplerHandle sampler = device->CreateSampler({
      .minFilter = Filter::Linear,
      .magFilter = Filter::Linear,
      .addressU = SamplerAddressMode::Repeat,
      .addressV = SamplerAddressMode::Repeat,
      .addressW = SamplerAddressMode::Repeat,
      .debugName = "Texture Sampler",
  });

  std::cout << "Creating descriptor set layout\n";
  BindingDesc bindings[] = {
      {
          .binding = 0,
          .type = BindingType::CombinedImageSampler,
          .count = 1,
          .visibility = ShaderStage::Fragment,
      },
  };

  BindingLayoutHandle setLayout = device->CreateBindingLayout({
      .bindings = bindings,
      .bindingCount = 1,
      .debugName = "Blending Test Set Layout",
  });

  std::cout << "Creating descriptor pool\n";
  BindingPoolSize poolSizes[] = {
      {
          .type = BindingType::CombinedImageSampler,
          .count = 1,
      },
  };

  BindingPoolHandle descriptorPool = device->CreateBindingPool({
      .poolSizes = poolSizes,
      .poolSizeCount = 1,
      .maxSets = 1,
      .debugName = "Blending Test Descriptor Pool",
  });

  std::cout << "Allocating descriptor set\n";
  BindingSetHandle descriptorSet = device->AllocateBindingSet(
      descriptorPool, setLayout, "Blending Test Set");

  std::cout << "Updating descriptor set\n";
  BindingImageInfo imageInfo{};
  imageInfo.sampler = sampler;
  imageInfo.imageView = textureView;
  imageInfo.imageLayout = ImageLayout::ShaderReadOnly;

  device->UpdateBindingSet({
      .dstSet = descriptorSet,
      .binding = 0,
      .arrayElement = 0,
      .type = BindingType::CombinedImageSampler,
      .bufferInfo = nullptr,
      .imageInfo = &imageInfo,
      .descriptorCount = 1,
  });

  auto vertSpv = ShaderCompiler::CompileFile({
      .path = "examples/textured_quad/textured_quad.vert",
      .stage = ShaderStage::Vertex,
      .entryPoint = "main",
  });

  auto fragSpv = ShaderCompiler::CompileFile({
      .path = "examples/textured_quad/textured_quad.frag",
      .stage = ShaderStage::Fragment,
      .entryPoint = "main",
  });

  ShaderHandle vertexShader = device->CreateShader({
      .stage = ShaderStage::Vertex,
      .bytecode = vertSpv.spirv.data(),
      .bytecodeSize =
          static_cast<u64>(vertSpv.spirv.size() * sizeof(std::uint32_t)),
      .entryPoint = "main",
      .reflection = vertSpv.reflection,
      .debugName = "Blending Test Vertex Shader",
  });

  ShaderHandle fragmentShader = device->CreateShader({
      .stage = ShaderStage::Fragment,
      .bytecode = fragSpv.spirv.data(),
      .bytecodeSize =
          static_cast<u64>(fragSpv.spirv.size() * sizeof(std::uint32_t)),
      .entryPoint = "main",
      .reflection = fragSpv.reflection,
      .debugName = "Blending Test Fragment Shader",
  });

  VertexBufferLayoutDesc vertexLayout{
      .stride = sizeof(Vertex),
      .inputRate = VertexInputRate::PerVertex,
      .attributes = {{
                         .location = 0,
                         .format = VertexFormat::Float32x3,
                         .offset = static_cast<u32>(offsetof(Vertex, pos)),
                     },
                     {
                         .location = 1,
                         .format = VertexFormat::Float32x2,
                         .offset = static_cast<u32>(offsetof(Vertex, uv)),
                     }},
  };

  BindingLayoutHandle setLayouts[] = {setLayout};

  GraphicsPipelineDesc pipelineDesc{};
  pipelineDesc.vertexShader = vertexShader;
  pipelineDesc.fragmentShader = fragmentShader;
  pipelineDesc.vertexLayouts.push_back(vertexLayout);
  pipelineDesc.layout.descriptorSetLayouts = setLayouts;
  pipelineDesc.layout.descriptorSetLayoutCount = 1;
  pipelineDesc.topology = PrimitiveTopology::TriangleList;
  pipelineDesc.raster.cullBackFaces = false;
  pipelineDesc.raster.frontFaceCCW = true;
  pipelineDesc.raster.wireframe = false;
  pipelineDesc.blend = {
      .enable = true,
      .srcColor = BlendFactor::SrcAlpha,
      .dstColor = BlendFactor::OneMinusSrcAlpha,
      .colorOp = BlendOp::Add,
      .srcAlpha = BlendFactor::One,
      .dstAlpha = BlendFactor::OneMinusSrcAlpha,
      .alphaOp = BlendOp::Add,
  };
  pipelineDesc.colorFormat = Format::BGRA8_UNORM;
  pipelineDesc.debugName = "Blending Test Pipeline";

  std::cout << "Creating pipeline\n";
  PipelineHandle pipeline = device->CreateGraphicsPipeline(pipelineDesc);

  std::cout << "Entering render loop\n";

  bool uploaded = false;
  float time = 0.0f;

  while (!app.GetWindow().ShouldClose()) {
    app.GetWindow().PollEvents();

    if (app.GetWindow().WasFramebufferResized()) {
      app.GetWindow().ResetFramebufferResizedFlag();

      const u32 fbWidth =
          static_cast<u32>(app.GetWindow().GetFramebufferWidth());
      const u32 fbHeight =
          static_cast<u32>(app.GetWindow().GetFramebufferHeight());

      if (fbWidth > 0 && fbHeight > 0) {
        device->WaitIdle();
        device->ResizeSwapchain(swapchain, fbWidth, fbHeight);
      }

      continue;
    }

    Extent2D dims = device->GetSwapchainDimensions();
    if (dims.width == 0 || dims.height == 0) {
      continue;
    }

    time += 0.016f;

    FrameBeginResult frame = device->BeginFrame(swapchain);
    if (!frame.success) {
      continue;
    }

    ICommandList &cmd = device->GetCommandList(frame.commandList);

    cmd.Begin();

    if (!uploaded) {
      std::cout << "Recording texture upload commands\n";

      BufferImageCopyRegion copyRegion{};
      copyRegion.bufferOffset = 0;
      copyRegion.bufferRowLength = 0;
      copyRegion.bufferImageHeight = 0;
      copyRegion.mipLevel = 0;
      copyRegion.baseArrayLayer = 0;
      copyRegion.layerCount = 1;
      copyRegion.imageOffset = {0, 0, 0};
      copyRegion.imageExtent = {
          static_cast<u32>(texWidth),
          static_cast<u32>(texHeight),
          1,
      };
      copyRegion.aspect = ImageAspect::Color;

      cmd.Barrier({
          .image = textureImage,
          .newLayout = ImageLayout::TransferDst,
          .aspect = ImageAspect::Color,
      });

      cmd.CopyBufferToImage(stagingBuffer, textureImage, copyRegion);

      cmd.Barrier({
          .image = textureImage,
          .newLayout = ImageLayout::ShaderReadOnly,
          .aspect = ImageAspect::Color,
      });

      uploaded = true;
    }

    cmd.Barrier({
        .image = frame.backbufferImage,
        .newLayout = ImageLayout::ColorAttachment,
        .aspect = ImageAspect::Color,
    });

    cmd.SetViewport({
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(dims.width),
        .height = static_cast<float>(dims.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    });

    cmd.SetScissor({
        .offset = {0, 0},
        .extent = {dims.width, dims.height},
    });

    ColorAttachmentDesc colorAttachment{};
    colorAttachment.view = frame.backbuffer;
    colorAttachment.loadOp = LoadOp::Clear;
    colorAttachment.storeOp = StoreOp::Store;
    colorAttachment.clearValue = {0.1f, 0.2f, 0.35f, 1.0f};

    RenderingInfo renderingInfo{};
    renderingInfo.renderArea = {{0, 0}, {dims.width, dims.height}};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.depthAttachment = nullptr;

    glm::mat4 model =
        glm::rotate(glm::mat4(1.0f), time, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 proj = glm::mat4(1.0f);
    glm::mat4 mvp = proj * view * model;

    cmd.BeginRendering(renderingInfo);
    cmd.BindPipeline(pipeline);
    cmd.SetBindings(pipeline, 0, descriptorSet);
    cmd.BindVertexBuffer(0, vertexBuffer, 0);
    cmd.BindIndexBuffer(indexBuffer, IndexType::U16, 0);
    cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(glm::mat4), &mvp);
    cmd.DrawIndexed(static_cast<u32>(indices.size()));
    cmd.EndRendering();

    cmd.Barrier({
        .image = frame.backbufferImage,
        .newLayout = ImageLayout::Present,
        .aspect = ImageAspect::Color,
    });

    cmd.End();

    device->SubmitAndPresent(frame.commandList, swapchain);
  }

  std::cout << "Shutting down\n";

  device->WaitIdle();

  device->DestroyPipeline(pipeline);
  device->DestroyShader(fragmentShader);
  device->DestroyShader(vertexShader);
  device->DestroyBindingPool(descriptorPool);
  device->DestroyBindingLayout(setLayout);
  device->DestroySampler(sampler);
  device->DestroyImageView(textureView);
  device->DestroyImage(textureImage);
  device->DestroyBuffer(stagingBuffer);
  device->DestroyBuffer(indexBuffer);
  device->DestroyBuffer(vertexBuffer);
  device->DestroySwapchain(swapchain);
  DestroyDevice(device);

  std::cout << "Shutdown complete\n";
  return 0;
}
