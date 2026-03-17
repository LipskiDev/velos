#include "core/window.h"
#include "rhi/rhi_command_list.h"
#include "rhi/rhi_types.h"
#include "rhi/vulkan/vk_device.h"
#include <cmath>
#include <core/application.h>
#include <exception>
#include <iostream>

#include <rhi/rhi_device.h>

#include "shader/shader_compiler.h"

using namespace Velos;

int main() {
  using namespace Velos::RHI;

  std::cout << "Creating app\n";
  Application app({
      .width = 1280,
      .height = 720,
      .title = "Velos Swapchain Test",
      .resizable = false,
  });

  std::cout << "Creating device\n";
  IDevice *device = CreateDevice({.backend = BackendAPI::Vulkan,
                                  .enableValidation = true,
                                  .applicationName = "Velos Vulkan Bootstrap"});

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

  auto vertSpv =
      ShaderCompiler::CompileFile({.path = "examples/triangle/triangle.vert",
                                   .stage = ShaderStage::Vertex,
                                   .entryPoint = "main"});

  auto fragSpv =
      ShaderCompiler::CompileFile({.path = "examples/triangle/triangle.frag",
                                   .stage = ShaderStage::Fragment,
                                   .entryPoint = "main"});

  ShaderHandle vertexShader =
      device->CreateShader({.stage = ShaderStage::Vertex,
                            .bytecode = vertSpv.spirv.data(),
                            .bytecodeSize = static_cast<u64>(
                                vertSpv.spirv.size() * sizeof(std::uint32_t)),
                            .entryPoint = "main",
                            .debugName = "Triangle Vertex Shader"});

  ShaderHandle fragmentShader =
      device->CreateShader({.stage = ShaderStage::Fragment,
                            .bytecode = fragSpv.spirv.data(),
                            .bytecodeSize = static_cast<u64>(
                                fragSpv.spirv.size() * sizeof(std::uint32_t)),
                            .entryPoint = "main",
                            .debugName = "Triangle Fragment Shader"});

  PipelineHandle pipeline = device->CreateGraphicsPipeline({
      .vertexShader = vertexShader,
      .fragmentShader = fragmentShader,
      .topology = PrimitiveTopology::TriangleList,
      .raster =
          {
              .cullBackFaces = false,
              .frontFaceCCW = true,
              .wireframe = false,
          },
      .colorFormat = Format::BGRA8_UNORM,
      .debugName = "Triangle Pipeline",
  });

  std::cout << "Entering render loop\n";

  float time = 0.0f;
  while (!app.GetWindow().ShouldClose()) {

    app.GetWindow().PollEvents();

    time += 0.016f; // or real delta later

    float color[4] = {(std::sin(time) * 0.5f + 0.5f),
                      (std::cos(time) * 0.5f + 0.5f), 0.2f, 1.0f};

    FrameBeginResult frame = device->BeginFrame(swapchain);
    if (!frame.success)
      continue;

    ICommandList &cmd = device->GetCommandList(frame.commandList);
    ColorAttachmentDesc colorAttachment{.texture = frame.backbuffer,
                                        .loadOp = LoadOp::Clear,
                                        .storeOp = StoreOp::Store,
                                        .clearValue = {0.1f, 0.1f, 0.2f, 1.0f}};

    RenderingInfo renderingInfo{.renderArea = {{0, 0}, {1280, 720}},
                                .colorAttachments = &colorAttachment,
                                .colorAttachmentCount = 1,
                                .depthAttachment = nullptr};

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
    cmd.PushConstants(ShaderStage::Fragment, 0, sizeof(color), color);
    cmd.Draw(3);
    cmd.EndRendering();

    vkDevice->TransitionCurrentSwapchainImageForPresent();

    cmd.End();

    device->SubmitAndPresent(frame.commandList, swapchain);
  }

  std::cout << "Shutting down\n";

  device->WaitIdle();

  device->DestroyShader(fragmentShader);
  device->DestroyShader(vertexShader);

  std::cout << "Destroying Swapchain\n";
  device->DestroySwapchain(swapchain);

  std::cout << "Destroying device\n";
  DestroyDevice(device);

  std::cout << "Shutdown complete\n";

  return 0;
}
