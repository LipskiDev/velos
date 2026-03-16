#include "rhi/rhi_command_list.h"
#include <core/application.h>
#include <exception>
#include <iostream>

#include <rhi/rhi_device.h>

using namespace Velos;

int main() {
  using namespace Velos::RHI;

  try {
    IDevice *device =
        CreateDevice({.backend = BackendAPI::Vulkan,
                      .enableValidation = true,
                      .applicationName = "Velos Vulkan Bootstrap"});

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
    DestroyDevice(device);

    std::cout << "Shutdown complete\n";
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
