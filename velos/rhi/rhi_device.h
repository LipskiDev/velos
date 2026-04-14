
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

struct FrameBeginResult {
  CommandListHandle commandList{};
  ImageViewHandle backbuffer{};
  ImageHandle backbufferImage{};
  u32 backbufferIndex = 0;
  bool success = false;
  uint32_t frameIndex;
  uint32_t imageIndex;
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
  virtual u64 GetBufferDeviceAddress(BufferHandle handle) const = 0;

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

  virtual DescriptorSetLayoutHandle
  CreateDescriptorSetLayout(const DescriptorSetLayoutDesc &desc) = 0;
  virtual void DestroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) = 0;

  virtual DescriptorPoolHandle
  CreateDescriptorPool(const DescriptorPoolDesc &desc) = 0;
  virtual void DestroyDescriptorPool(DescriptorPoolHandle handle) = 0;
  virtual DescriptorSetHandle
  AllocateDescriptorSet(DescriptorPoolHandle pool,
                        DescriptorSetLayoutHandle layout,
                        const char *debugName = nullptr) = 0;

  virtual void UpdateDescriptorSet(const WriteDescriptorDesc &desc) = 0;

  virtual ImageLayout GetImageLayout(ImageHandle image) const = 0;

  virtual FrameBeginResult BeginFrame(SwapchainHandle handle) = 0;
  virtual ICommandList &GetCommandList() = 0;
  virtual void Submit() = 0;
  virtual void SubmitAndPresent(SwapchainHandle swapchain) = 0;

  virtual void WaitIdle() = 0;
  virtual void CollectGarbage() = 0;

public:
  virtual Extent2D GetSwapchainDimensions() const = 0;
};

IDevice *CreateDevice(const DeviceDesc &desc);
void DestroyDevice(IDevice *device);
} // namespace Velos::RHI
