
#pragma once

#include "rhi/upload_context.h"
#include "command_list.h"
#include "handles.h"
#include "pipeline.h"
#include "resources.h"
#include "types.h"
#include <memory>

namespace Velos::RHI {
enum class GraphicsAPI {
  Vulkan,
  // D3D12,
  // Metal
};

struct DeviceDesc {
  GraphicsAPI graphicsAPI = GraphicsAPI::Vulkan;
  bool enableValidation = true;
  const char *applicationName = "Velos";
};

struct FrameBeginResult {
  CommandListHandle commandList{};
  ImageViewHandle backbuffer{};
  ImageHandle backbufferImage{};
  u32 backbufferIndex = 0;
  bool success = false;
  uint32_t frameIndex = 0;
  uint32_t imageIndex = 0;
  float frameFenceWaitMs = 0.0f;
  float acquireImageMs = 0.0f;
  float imageFenceWaitMs = 0.0f;
};

class IDevice {
public:
  virtual ~IDevice() = default;

  virtual GraphicsAPI GetBackend() const = 0;

  virtual SwapchainHandle CreateSwapchain(const SwapchainDesc &desc) = 0;
  virtual void DestroySwapchain(SwapchainHandle handle) = 0;
  virtual void ResizeSwapchain(SwapchainHandle handle, u32 width,
                               u32 height) = 0;

  virtual BufferHandle CreateBuffer(const BufferDesc &desc) = 0;
  virtual void DestroyBuffer(BufferHandle handle) = 0;
  virtual u64 GetBufferDeviceAddress(BufferHandle handle) const = 0;
  virtual void *MapBuffer(BufferHandle handle) = 0;
  virtual void UnmapBuffer(BufferHandle handle) = 0;

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
  virtual PipelineHandle
  CreateComputePipeline(const ComputePipelineDesc &desc) = 0;
  virtual void DestroyPipeline(PipelineHandle handle) = 0;

  virtual BindingLayoutHandle
  CreateBindingLayout(const BindingLayoutDesc &desc) = 0;
  virtual void DestroyBindingLayout(BindingLayoutHandle handle) = 0;

  virtual BindingPoolHandle
  CreateBindingPool(const BindingPoolDesc &desc) = 0;
  virtual void DestroyBindingPool(BindingPoolHandle handle) = 0;
  virtual BindingSetHandle
  AllocateBindingSet(const BindingSetAllocationDesc& desc) = 0;

  virtual void UpdateBindingSet(const BindingWriteDesc &desc) = 0;

  virtual QueryPoolHandle CreateTimestampQueryPool(
      const QueryPoolDesc &desc) = 0;
  virtual void DestroyQueryPool(QueryPoolHandle handle) = 0;
  virtual bool GetTimestampQueryResults(QueryPoolHandle handle, u32 firstQuery,
                                        u32 queryCount, u64 *results) = 0;
  virtual double GetTimestampPeriodNanoseconds() const = 0;
  virtual u32 GetCurrentFrameIndex() const = 0;

  virtual ImageLayout GetImageLayout(ImageHandle image, u32 mipLevel) const = 0;

  virtual FrameBeginResult BeginFrame(SwapchainHandle handle) = 0;
  virtual ICommandList &GetCommandList() = 0;
  virtual void Submit() = 0;
  virtual void SubmitAndPresent(SwapchainHandle swapchain) = 0;

  virtual void WaitIdle() = 0;
  virtual void CollectGarbage() = 0;

  virtual std::unique_ptr<IUploadContext>
  CreateUploadContext(u64 stagingBufferSize = 16 * 1024 * 1024) = 0;

  virtual GeneratedPipelineLayout BuildPipelineLayout(
      const PipelineReflectionData& reflection,
      const PipelineLayoutOverrides& overrides = {}
  ) = 0;

public:
  virtual Extent2D GetSwapchainDimensions() const = 0;
};

IDevice *CreateDevice(const DeviceDesc &desc);
void DestroyDevice(IDevice *device);
} // namespace Velos::RHI
