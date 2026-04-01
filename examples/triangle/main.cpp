#include "core/window.h"
#include "rhi/rhi_command_list.h"
#include "rhi/rhi_device.h"
#include "rhi/rhi_pipeline.h"
#include "rhi/rhi_types.h"
#include "shader/shader_compiler.h"

#include <core/application.h>

#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>

using namespace Velos;

int main() {
  using namespace Velos::RHI;

  std::cout << "Creating app\n";
  Application app({
      .width = 1280,
      .height = 720,
      .title = "Velos Triangle",
      .resizable = true,
  });

  std::cout << "Creating device\n";
  IDevice *device = CreateDevice({
      .backend = BackendAPI::Vulkan,
      .enableValidation = true,
      .applicationName = "Velos Triangle",
  });

  std::cout << "Creating swapchain\n";
  SwapchainHandle swapchain = device->CreateSwapchain({
      .windowHandle = app.GetWindow().GetNativeHandle(),
      .width = static_cast<u32>(app.GetWindow().GetFramebufferWidth()),
      .height = static_cast<u32>(app.GetWindow().GetFramebufferHeight()),
      .format = Format::BGRA8_UNORM,
      .bufferCount = 2,
      .vsync = true,
      .debugName = "Main Swapchain",
  });

  auto vertSpv = ShaderCompiler::CompileFile({
      .path = "examples/triangle/triangle.vert",
      .stage = ShaderStage::Vertex,
      .entryPoint = "main",
  });

  auto fragSpv = ShaderCompiler::CompileFile({
      .path = "examples/triangle/triangle.frag",
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
      .debugName = "Triangle Vertex Shader",
  });

  ShaderHandle fragmentShader = device->CreateShader({
      .stage = ShaderStage::Fragment,
      .bytecode = fragSpv.spirv.data(),
      .bytecodeSize =
          static_cast<u64>(fragSpv.spirv.size() * sizeof(std::uint32_t)),
      .entryPoint = "main",
      .reflection = fragSpv.reflection,
      .debugName = "Triangle Fragment Shader",
  });

  GraphicsPipelineDesc pipelineDesc{};
  pipelineDesc.vertexShader = vertexShader;
  pipelineDesc.fragmentShader = fragmentShader;
  pipelineDesc.topology = PrimitiveTopology::TriangleList;
  pipelineDesc.raster.cullBackFaces = false;
  pipelineDesc.raster.frontFaceCCW = true;
  pipelineDesc.raster.wireframe = false;
  pipelineDesc.colorFormat = Format::BGRA8_UNORM;
  pipelineDesc.debugName = "Triangle Pipeline";

  std::cout << "Creating pipeline\n";
  PipelineHandle pipeline = device->CreateGraphicsPipeline(pipelineDesc);

  std::cout << "Entering render loop\n";

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

    float color[4] = {
        std::sin(time) * 0.5f + 0.5f,
        std::cos(time) * 0.5f + 0.5f,
        0.2f,
        1.0f,
    };

    FrameBeginResult frame = device->BeginFrame(swapchain);
    if (!frame.success) {
      continue;
    }

    ICommandList &cmd = device->GetCommandList(frame.commandList);

    cmd.Begin();

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
    colorAttachment.clearValue = {0.1f, 0.1f, 0.2f, 1.0f};

    RenderingInfo renderingInfo{};
    renderingInfo.renderArea = {{0, 0}, {dims.width, dims.height}};
    renderingInfo.colorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.depthAttachment = nullptr;

    cmd.BeginRendering(renderingInfo);
    cmd.BindPipeline(pipeline);
    cmd.PushConstants(ShaderStage::Fragment, 0, sizeof(color), color);
    cmd.Draw(3);
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
  device->DestroySwapchain(swapchain);
  DestroyDevice(device);

  std::cout << "Shutdown complete\n";
  return 0;
}
