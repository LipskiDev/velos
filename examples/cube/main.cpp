#include "core/window.h"
#include "rhi/rhi_command_list.h"
#include "rhi/rhi_device.h"
#include "rhi/rhi_pipeline.h"
#include "rhi/rhi_types.h"
#include "rhi/vulkan/vk_device.h"
#include "shader/shader_compiler.h"

#include <core/application.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <vector>

using namespace Velos;

struct Vertex {
  glm::vec3 pos;
  glm::vec3 color;
};

int main() {
  using namespace Velos::RHI;

  std::cout << "Creating app\n";
  Application app({
      .width = 1280,
      .height = 720,
      .title = "Velos Cube",
      .resizable = false,
  });

  std::cout << "Creating device\n";
  IDevice *device = CreateDevice({
      .backend = BackendAPI::Vulkan,
      .enableValidation = true,
      .applicationName = "Velos Cube",
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

  VulkanDevice *vkDevice = dynamic_cast<VulkanDevice *>(device);
  if (!vkDevice) {
    throw std::runtime_error("Device is not a VulkanDevice");
  }

  std::vector<Vertex> vertices = {
      // Front (+Z)
      {{-0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}},
      {{0.5f, -0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
      {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
      {{-0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}},
      {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
      {{-0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}},

      // Back (-Z)
      {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}},
      {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}},
      {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}},
      {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}},
      {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}},
      {{0.5f, 0.5f, -0.5f}, {0.3f, 0.3f, 1.0f}},

      // Left (-X)
      {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.4f, 0.4f}},
      {{-0.5f, -0.5f, 0.5f}, {0.4f, 1.0f, 0.4f}},
      {{-0.5f, 0.5f, 0.5f}, {0.4f, 0.4f, 1.0f}},
      {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.4f, 0.4f}},
      {{-0.5f, 0.5f, 0.5f}, {0.4f, 0.4f, 1.0f}},
      {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 0.4f}},

      // Right (+X)
      {{0.5f, -0.5f, 0.5f}, {1.0f, 0.5f, 0.0f}},
      {{0.5f, -0.5f, -0.5f}, {0.0f, 0.8f, 1.0f}},
      {{0.5f, 0.5f, -0.5f}, {0.8f, 0.0f, 1.0f}},
      {{0.5f, -0.5f, 0.5f}, {1.0f, 0.5f, 0.0f}},
      {{0.5f, 0.5f, -0.5f}, {0.8f, 0.0f, 1.0f}},
      {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.5f}},

      // Top (+Y)
      {{-0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.5f}},
      {{0.5f, 0.5f, 0.5f}, {0.0f, 0.5f, 1.0f}},
      {{0.5f, 0.5f, -0.5f}, {0.5f, 1.0f, 0.0f}},
      {{-0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.5f}},
      {{0.5f, 0.5f, -0.5f}, {0.5f, 1.0f, 0.0f}},
      {{-0.5f, 0.5f, -0.5f}, {1.0f, 0.8f, 0.2f}},

      // Bottom (-Y)
      {{-0.5f, -0.5f, -0.5f}, {0.7f, 0.2f, 0.2f}},
      {{0.5f, -0.5f, -0.5f}, {0.2f, 0.7f, 0.2f}},
      {{0.5f, -0.5f, 0.5f}, {0.2f, 0.2f, 0.7f}},
      {{-0.5f, -0.5f, -0.5f}, {0.7f, 0.2f, 0.2f}},
      {{0.5f, -0.5f, 0.5f}, {0.2f, 0.2f, 0.7f}},
      {{-0.5f, -0.5f, 0.5f}, {0.7f, 0.7f, 0.2f}},
  };

  std::cout << "Creating vertex buffer\n";
  BufferDesc bufferDesc{.size =
                            static_cast<u64>(vertices.size() * sizeof(Vertex)),
                        .usage = BufferUsage::Vertex,
                        .memoryUsage = MemoryUsage::CPUToGPU,
                        .initialData = vertices.data(),
                        .debugName = "Cube Vertex Buffer"};

  BufferHandle vertexBuffer = device->CreateBuffer(bufferDesc);

  auto vertSpv = ShaderCompiler::CompileFile({.path = "examples/cube/cube.vert",
                                              .stage = ShaderStage::Vertex,
                                              .entryPoint = "main"});

  auto fragSpv = ShaderCompiler::CompileFile({.path = "examples/cube/cube.frag",
                                              .stage = ShaderStage::Fragment,
                                              .entryPoint = "main"});

  ShaderHandle vertexShader =
      device->CreateShader({.stage = ShaderStage::Vertex,
                            .bytecode = vertSpv.spirv.data(),
                            .bytecodeSize = static_cast<u64>(
                                vertSpv.spirv.size() * sizeof(std::uint32_t)),
                            .entryPoint = "main",
                            .reflection = vertSpv.reflection,
                            .debugName = "Cube Vertex Shader"});

  ShaderHandle fragmentShader =
      device->CreateShader({.stage = ShaderStage::Fragment,
                            .bytecode = fragSpv.spirv.data(),
                            .bytecodeSize = static_cast<u64>(
                                fragSpv.spirv.size() * sizeof(std::uint32_t)),
                            .entryPoint = "main",
                            .reflection = fragSpv.reflection,
                            .debugName = "Cube Fragment Shader"});

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
                         .offset = static_cast<u32>(offsetof(Vertex, color)),
                     }

      }};

  GraphicsPipelineDesc pipelineDesc{};
  pipelineDesc.vertexShader = vertexShader;
  pipelineDesc.fragmentShader = fragmentShader;
  pipelineDesc.vertexLayouts.push_back(vertexLayout);
  pipelineDesc.topology = PrimitiveTopology::TriangleList;
  pipelineDesc.raster.cullBackFaces = false;
  pipelineDesc.raster.frontFaceCCW = true;
  pipelineDesc.raster.wireframe = false;
  pipelineDesc.colorFormat = Format::BGRA8_UNORM;
  pipelineDesc.debugName = "Cube Pipeline";

  PipelineHandle pipeline = device->CreateGraphicsPipeline(pipelineDesc);

  std::cout << "Entering render loop\n";

  float time = 0.0f;
  while (!app.GetWindow().ShouldClose()) {
    app.GetWindow().PollEvents();

    time += 0.016f;

    FrameBeginResult frame = device->BeginFrame(swapchain);
    if (!frame.success) {
      continue;
    }

    ICommandList &cmd = device->GetCommandList(frame.commandList);

    ColorAttachmentDesc colorAttachment{};
    colorAttachment.texture = frame.backbuffer;
    colorAttachment.loadOp = LoadOp::Clear;
    colorAttachment.storeOp = StoreOp::Store;
    colorAttachment.clearValue = {0.08f, 0.08f, 0.12f, 1.0f};

    RenderingInfo renderingInfo{};
    renderingInfo.renderArea = {{0, 0}, {1280, 720}};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.depthAttachment = nullptr;

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, time, glm::vec3(0.4f, 1.0f, 0.2f));

    glm::mat4 view =
        glm::lookAt(glm::vec3(0.0f, 0.0f, 2.5f), glm::vec3(0.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 proj =
        glm::perspective(glm::radians(60.0f), 1280.0f / 720.0f, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    glm::mat4 mvp = proj * view * model;

    cmd.Begin();

    cmd.SetViewport({.x = 0.0f,
                     .y = 0.0f,
                     .width = 1280.0f,
                     .height = 720.0f,
                     .minDepth = 0.0f,
                     .maxDepth = 1.0f});

    cmd.SetScissor({.offset = {0, 0}, .extent = {1280, 720}});

    vkDevice->TransitionCurrentSwapchainImageForRendering();

    cmd.BeginRendering(renderingInfo);
    cmd.BindPipeline(pipeline);
    cmd.BindVertexBuffer(0, vertexBuffer, 0);
    cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(glm::mat4), &mvp);
    cmd.Draw(static_cast<u32>(vertices.size()));
    cmd.EndRendering();

    vkDevice->TransitionCurrentSwapchainImageForPresent();

    cmd.End();

    device->SubmitAndPresent(frame.commandList, swapchain);
  }

  std::cout << "Shutting down\n";

  device->WaitIdle();

  device->DestroyPipeline(pipeline);
  device->DestroyBuffer(vertexBuffer);
  device->DestroyShader(fragmentShader);
  device->DestroyShader(vertexShader);
  device->DestroySwapchain(swapchain);
  DestroyDevice(device);

  std::cout << "Shutdown complete\n";
  return 0;
}
