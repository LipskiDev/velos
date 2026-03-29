
#pragma once

#include "rhi_command_list.h"
#include "rhi_handles.h"
#include "rhi_pipeline.h"
#include "rhi_resources.h"
#include "rhi_types.h"

namespace Velos::RHI {
enum class BackendAPI {
  Vulkan,
  // D3D12,
  // Metal
};

struct DeviceDesc {
  BackendAPI backend = BackendAPI::Vulkan;
  bool enableValidation = true;
  const char *applicationName = "Velos";
};

struct SwapchainDesc {
  void *windowHandle = nullptr;
  u32 width = 0;
  u32 height = 0;
  Format format = Format::BGRA8_UNORM;
  u32 bufferCount = 2;
  bool vsync = true;
  const char *debugName = nullptr;
};

struct FrameBeginResult {
  CommandListHandle commandList{};
  TextureHandle backbuffer{};
  u32 backbufferIndex = 0;
  bool success = false;
};

class IDevice {
public:
  virtual ~IDevice() = default;

  virtual BackendAPI GetBackend() const = 0;

  virtual SwapchainHandle CreateSwapchain(const SwapchainDesc &desc) = 0;
  virtual void DestroySwapchain(SwapchainHandle handle) = 0;
  virtual void ResizeSwapchain(SwapchainHandle handle, u32 width,
                               u32 height) = 0;

  virtual BufferHandle CreateBuffer(const BufferDesc &desc) = 0;
  virtual void DestroyBuffer(BufferHandle handle) = 0;

  virtual TextureHandle CreateTexture(const TextureDesc &desc) = 0;
  virtual void DestroyTexture(TextureHandle handle) = 0;

  virtual ImageHandle CreateImage(const ImageDesc &desc) = 0;
  virtual void DestroyImage(ImageHandle image) = 0;

  virtual ImageViewHandle CreateImageView(const ImageViewDesc &desc) = 0;
  virtual void DestroyImageView(ImageViewHandle view) = 0;

  virtual SamplerHandle CreateSampler(const SamplerDesc &desc) = 0;
  virtual void DestroySampler(SamplerHandle handle) = 0;

  virtual ShaderHandle CreateShader(const ShaderDesc &desc) = 0;
  virtual void DestroyShader(ShaderHandle handle) = 0;

  virtual PipelineHandle
  CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) = 0;
  virtual void DestroyPipeline(PipelineHandle handle) = 0;

  virtual FrameBeginResult BeginFrame(SwapchainHandle handle) = 0;
  virtual ICommandList &GetCommandList(CommandListHandle handle) = 0;
  virtual void SubmitAndPresent(CommandListHandle commandList,
                                SwapchainHandle swapchain) = 0;

  virtual void WaitIdle() = 0;
  virtual void CollectGarbage() = 0;
};

IDevice *CreateDevice(const DeviceDesc &desc);
void DestroyDevice(IDevice *device);
} // namespace Velos::RHI
