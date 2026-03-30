#pragma once

#include <vector>

#include <rhi/rhi_command_list.h>
#include <rhi/rhi_device.h>

struct ImDrawData;

namespace VRHI = Velos::RHI;

class ImGuiRenderer {
public:
  void Initialize(VRHI::IDevice *device, VRHI::SwapchainHandle swapchain,
                  VRHI::Format colorFormat, VRHI::Format depthFormat);
  void Shutdown();

  void NewFrame(float deltaSeconds, int fbWidth, int fbHeight);
  void Render(VRHI::ICommandList &cmd, ImDrawData *drawData);

  void OnResize(int fbWidth, int fbHeight);

private:
  struct FrameResources {
    VRHI::BufferHandle vertexBuffer;
    VRHI::BufferHandle indexBuffer;

    Velos::u32 vertexCapacityBytes = 0;
    Velos::u32 indexCapacityBytes = 0;
  };

  VRHI::IDevice *device_ = nullptr;
  VRHI::PipelineHandle pipeline_;
  VRHI::SamplerHandle fontSampler_;
  VRHI::ImageHandle fontImage_;
  VRHI::ImageViewHandle fontImageView_;
  VRHI::DescriptorSetLayoutHandle setLayout_;
  VRHI::DescriptorPoolHandle pool_;
  VRHI::DescriptorSetHandle fontSet_;

  FrameResources frame_;
};
