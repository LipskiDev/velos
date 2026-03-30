#include "core/window.h"
#include "rhi/rhi_command_list.h"
#include "rhi/rhi_device.h"
#include "rhi/rhi_pipeline.h"
#include "rhi/rhi_types.h"
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
      .resizable = false,
  });

  std::cout << "Creating device\n";
  IDevice *device = CreateDevice({
      .backend = BackendAPI::Vulkan,
      .enableValidation = true,
      .applicationName = "Velos Textured Quad",
  });

  std::cout << "Creating swapchain\n";
  SwapchainHandle swapchain = device->CreateSwapchain({
      .windowHandle = app.GetWindow().GetNativeHandle(),
      .width = static_cast<u32>(app.GetWindow().GetWidth()),
      .height = static_cast<u32>(app.GetWindow().GetHeight()),
      .format = Format::RGBA8_UNORM,
      .bufferCount = 2,
      .vsync = true,
      .debugName = "Main Swapchain",
  });

  std::vector<Vertex> vertices = {
      {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}}, {{0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}},
      {{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}},

      {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}}, {{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}},
      {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f}},
  };

  std::cout << "Creating vertex buffer\n";
  BufferHandle vertexBuffer = device->CreateBuffer({
      .size = static_cast<u64>(vertices.size() * sizeof(Vertex)),
      .usage = BufferUsage::Vertex,
      .memoryUsage = MemoryUsage::CPUToGPU,
      .initialData = vertices.data(),
      .debugName = "Quad Vertex Buffer",
  });

  std::cout << "Loading texture with stb_image\n";
  stbi_set_flip_vertically_on_load(1);

  int texWidth = 0;
  int texHeight = 0;
  int texChannels = 0;
  stbi_uc *pixels =
      stbi_load("examples/textured_quad/awesomeface.png", &texWidth, &texHeight,
                &texChannels, STBI_rgb_alpha);

  if (!pixels) {
    throw std::runtime_error("Failed to load texture.png with stb_image");
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
  DescriptorBindingDesc bindings[] = {
      {
          .binding = 0,
          .type = DescriptorType::CombinedImageSampler,
          .count = 1,
          .visibility = ShaderStage::Fragment,
      },
  };

  DescriptorSetLayoutHandle setLayout = device->CreateDescriptorSetLayout({
      .bindings = bindings,
      .bindingCount = 1,
      .debugName = "Textured Quad Set Layout",
  });

  std::cout << "Creating descriptor pool\n";
  DescriptorPoolSize poolSizes[] = {
      {
          .type = DescriptorType::CombinedImageSampler,
          .count = 1,
      },
  };

  DescriptorPoolHandle descriptorPool = device->CreateDescriptorPool({
      .poolSizes = poolSizes,
      .poolSizeCount = 1,
      .maxSets = 1,
      .debugName = "Textured Quad Descriptor Pool",
  });

  std::cout << "Allocating descriptor set\n";
  DescriptorSetHandle descriptorSet = device->AllocateDescriptorSet(
      descriptorPool, setLayout, "Textured Quad Set");

  std::cout << "Updating descriptor set\n";
  DescriptorImageInfo imageInfo{};
  imageInfo.sampler = sampler;
  imageInfo.imageView = textureView;
  imageInfo.imageLayout = ImageLayout::ShaderReadOnly;

  device->UpdateDescriptorSet({
      .dstSet = descriptorSet,
      .binding = 0,
      .arrayElement = 0,
      .type = DescriptorType::CombinedImageSampler,
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
      .debugName = "Textured Quad Vertex Shader",
  });

  ShaderHandle fragmentShader = device->CreateShader({
      .stage = ShaderStage::Fragment,
      .bytecode = fragSpv.spirv.data(),
      .bytecodeSize =
          static_cast<u64>(fragSpv.spirv.size() * sizeof(std::uint32_t)),
      .entryPoint = "main",
      .reflection = fragSpv.reflection,
      .debugName = "Textured Quad Fragment Shader",
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

  DescriptorSetLayoutHandle setLayouts[] = {setLayout};

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
  pipelineDesc.colorFormat = Format::BGRA8_UNORM;
  pipelineDesc.debugName = "Textured Quad Pipeline";

  std::cout << "Creating pipeline\n";
  PipelineHandle pipeline = device->CreateGraphicsPipeline(pipelineDesc);

  std::cout << "Entering render loop\n";

  bool uploaded = false;
  float time = 0.0f;

  while (!app.GetWindow().ShouldClose()) {
    app.GetWindow().PollEvents();
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
          .oldLayout = ImageLayout::Undefined,
          .newLayout = ImageLayout::TransferDst,
          .aspect = ImageAspect::Color,
      });

      cmd.CopyBufferToImage(stagingBuffer, textureImage, copyRegion);

      cmd.Barrier({
          .image = textureImage,
          .oldLayout = ImageLayout::TransferDst,
          .newLayout = ImageLayout::ShaderReadOnly,
          .aspect = ImageAspect::Color,
      });

      uploaded = true;
    }

    ImageLayout currentLayout = device->GetImageLayout(frame.backbufferImage);

    cmd.Barrier({
        .image = frame.backbufferImage,
        .oldLayout = currentLayout,
        .newLayout = ImageLayout::ColorAttachment,
        .aspect = ImageAspect::Color,
    });

    cmd.SetViewport({
        .x = 0.0f,
        .y = 0.0f,
        .width = 1280.0f,
        .height = 720.0f,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    });

    cmd.SetScissor({
        .offset = {0, 0},
        .extent = {1280, 720},
    });

    ColorAttachmentDesc colorAttachment{};
    colorAttachment.view = frame.backbuffer;
    colorAttachment.loadOp = LoadOp::Clear;
    colorAttachment.storeOp = StoreOp::Store;
    colorAttachment.clearValue = {0.08f, 0.08f, 0.12f, 1.0f};

    RenderingInfo renderingInfo{};
    renderingInfo.renderArea = {{0, 0}, {1280, 720}};
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
    cmd.BindDescriptorSet(pipeline, 0, descriptorSet);
    cmd.BindVertexBuffer(0, vertexBuffer, 0);
    cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(glm::mat4), &mvp);
    cmd.Draw(static_cast<u32>(vertices.size()));
    cmd.EndRendering();

    cmd.Barrier({
        .image = frame.backbufferImage,
        .oldLayout = ImageLayout::ColorAttachment,
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
  device->DestroyDescriptorPool(descriptorPool);
  device->DestroyDescriptorSetLayout(setLayout);
  device->DestroySampler(sampler);
  device->DestroyImageView(textureView);
  device->DestroyImage(textureImage);
  device->DestroyBuffer(stagingBuffer);
  device->DestroyBuffer(vertexBuffer);
  device->DestroySwapchain(swapchain);
  DestroyDevice(device);

  std::cout << "Shutdown complete\n";
  return 0;
}
