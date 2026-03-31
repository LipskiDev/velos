#include "core/application.h"
#include "core/input_system.h"
#include "core/window.h"

#include "imgui_input_bridge.h"
#include "imgui_renderer.h"

#include "rhi/rhi_command_list.h"
#include "rhi/rhi_device.h"
#include "rhi/rhi_types.h"

#include <imgui.h>

#include <chrono>
#include <exception>
#include <iostream>

using namespace Velos;
using namespace Velos::RHI;

int main() {
  try {
    Application app({
        .width = 1280,
        .height = 720,
        .title = "Velos ImGui Demo",
        .resizable = false,
    });

    IDevice *device = CreateDevice({
        .backend = BackendAPI::Vulkan,
        .enableValidation = true,
        .applicationName = "Velos ImGui Demo",
    });

    SwapchainHandle swapchain = device->CreateSwapchain({
        .windowHandle = app.GetWindow().GetNativeHandle(),
        .width = static_cast<u32>(app.GetWindow().GetFramebufferWidth()),
        .height = static_cast<u32>(app.GetWindow().GetFramebufferHeight()),
        .format = Format::BGRA8_UNORM,
        .bufferCount = 2,
        .debugName = "Main Swapchain",
    });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    ImGuiRenderer imguiRenderer;
    imguiRenderer.Initialize(device, swapchain, Format::BGRA8_UNORM,
                             Format::Undefined);

    using Clock = std::chrono::high_resolution_clock;
    auto lastTime = Clock::now();

    bool showDemoWindow = true;

    while (!app.GetWindow().ShouldClose()) {
      InputSystem &input = app.GetInput();

      input.BeginFrame();
      app.GetWindow().PollEvents();

      auto now = Clock::now();
      float deltaTime = std::chrono::duration<float>(now - lastTime).count();
      lastTime = now;

      ImGuiInputBridge::Apply(input);
      imguiRenderer.NewFrame(deltaTime, app.GetWindow().GetWidth(),
                             app.GetWindow().GetHeight(),
                             app.GetWindow().GetFramebufferWidth(),
                             app.GetWindow().GetFramebufferHeight());

      ImGui::ShowDemoWindow(&showDemoWindow);

      ImGui::Begin("Velos");
      ImGui::Text("Custom Dear ImGui renderer on top of Velos");
      ImGui::Text("Backend: Vulkan");
      ImGui::Separator();
      ImGui::Text("Window: %d x %d", app.GetWindow().GetWidth(),
                  app.GetWindow().GetHeight());

      ImGui::Text("Framebuffer: %d x %d", app.GetWindow().GetFramebufferWidth(),
                  app.GetWindow().GetFramebufferHeight());
      ImGui::Text("Delta Time: %.4f", deltaTime);
      ImGui::Text("Mouse: %.1f, %.1f", input.GetMouseX(), input.GetMouseY());
      ImGui::End();

      ImGui::Render();

      FrameBeginResult frame = device->BeginFrame(swapchain);
      if (!frame.success) {
        continue;
      }

      ICommandList &cmd = device->GetCommandList(frame.commandList);

      cmd.Begin();

      cmd.Barrier(ImageBarrier{
          .image = frame.backbufferImage,
          .newLayout = ImageLayout::ColorAttachment,
          .aspect = ImageAspect::Color,
      });

      ColorAttachmentDesc colorAttachment{};
      colorAttachment.view = frame.backbuffer;
      colorAttachment.loadOp = LoadOp::Clear;
      colorAttachment.storeOp = StoreOp::Store;
      colorAttachment.clearValue = {
          .r = 0.1f,
          .g = 0.1f,
          .b = 0.1f,
          .a = 1.0f,
      };

      u32 fbWidth = static_cast<u32>(app.GetWindow().GetFramebufferWidth());
      u32 fbHeight = static_cast<u32>(app.GetWindow().GetFramebufferHeight());

      Rect2D renderArea{};
      renderArea.offset = {0, 0};
      renderArea.extent = {fbWidth, fbHeight};

      RenderingInfo renderingInfo{};
      renderingInfo.renderArea = renderArea;
      renderingInfo.colorAttachments = &colorAttachment;
      renderingInfo.colorAttachmentCount = 1;
      renderingInfo.depthAttachment = nullptr;

      cmd.BeginRendering(renderingInfo);

      cmd.SetViewport({
          .x = 0.0f,
          .y = 0.0f,
          .width = static_cast<float>(fbWidth),
          .height = static_cast<float>(fbHeight),
          .minDepth = 0.0f,
          .maxDepth = 1.0f,
      });

      cmd.SetScissor({
          .offset = {0, 0},
          .extent = {fbWidth, fbHeight},
      });

      imguiRenderer.Render(cmd, ImGui::GetDrawData());

      cmd.EndRendering();

      cmd.Barrier(ImageBarrier{
          .image = frame.backbufferImage,
          .newLayout = ImageLayout::Present,
          .aspect = ImageAspect::Color,
      });

      cmd.End();

      device->SubmitAndPresent(frame.commandList, swapchain);
    }

    device->WaitIdle();

    imguiRenderer.Shutdown();
    ImGui::DestroyContext();

    device->DestroySwapchain(swapchain);
    DestroyDevice(device);

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return 1;
  }
}
