#include "core/window.h"
#include "rhi/rhi_command_list.h"
#include <core/application.h>
#include <exception>
#include <iostream>

#include <rhi/rhi_device.h>

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

  FrameBeginResult frame = device->BeginFrame(SwapchainHandle{});
  ICommandList &cmd = device->GetCommandList(frame.commandList);

  cmd.Begin();
  cmd.SetViewport({.x = 0.0f,
                   .y = 0.0f,
                   .width = 1280.0f,
                   .height = 720.0f,
                   .minDepth = 0.0f,
                   .maxDepth = 1.0f});

  cmd.SetScissor({.offset = {0, 0}, .extent = {1280, 720}});
  cmd.End();

  device->SubmitAndPresent(frame.commandList, SwapchainHandle{});

  std::cout << "Command list record + submit worked\n";

  device->WaitIdle();

  std::cout << "Destroying Swapchain\n";
  device->DestroySwapchain(swapchain);

  std::cout << "Destroying device\n";
  DestroyDevice(device);

  std::cout << "Shutdown complete\n";

  return 0;
}
