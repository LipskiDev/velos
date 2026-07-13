#pragma once

#include <rhi/vulkan/profiler.h>

#include <unordered_map>
#include <vk_mem_alloc.h>

#include "rhi/device.h"
#include "rhi/types.h"
#include "rhi/vulkan/profiler.h"
#include "rhi/vulkan/swapchain.h"
#include "shader/shader_compiler.h"

namespace Velos::Vulkan {
using namespace Velos::RHI;
class CommandList;

struct Shader {
  VkShaderModule module = VK_NULL_HANDLE;
  ShaderStage stage = ShaderStage::None;
  ShaderReflectionData reflection;
};

struct Pipeline {
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
};

struct VulkanTexture {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  Format format = Format::Undefined;
  bool owned = false;
};

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;

  Format format = Format::Undefined;
  ImageType type = ImageType::Image2D;
  ImageUsage usage = ImageUsage::None;
  std::vector<ImageLayout> mipLayouts;

  u32 width = 0;
  u32 height = 0;
  u32 depth = 1;
  u32 mipLevels = 1;
  u32 arrayLayers = 1;

  bool owned = true;
};

struct ImageView {
  VkImageView view = VK_NULL_HANDLE;
  ImageHandle image;
  Format format = Format::Undefined;
  ImageAspect aspect = ImageAspect::Color;

  bool owned = true;
};

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VmaAllocationInfo allocationInfo{};

  u64 size = 0;
  u64 deviceAddress = 0;

  BufferUsage usage = BufferUsage::None;
  MemoryUsage memoryUsage = MemoryUsage::GPUOnly;
};

struct Sampler {
  VkSampler sampler = VK_NULL_HANDLE;
};

struct BindingLayout {
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  bool usesUpdateAfterBind = false;
};

struct BindingPool {
  VkDescriptorPool pool = VK_NULL_HANDLE;
  bool supportsUpdateAfterBind = false;
};

struct BindingSet {
  VkDescriptorSet set = VK_NULL_HANDLE;
  BindingLayoutHandle layout;
  BindingPoolHandle pool;
};

class Device final : public IDevice {
public:
  explicit Device(const DeviceDesc &desc);
  ~Device() override;

  void CreateAllocator();

  GraphicsAPI GetBackend() const override;

  SwapchainHandle CreateSwapchain(const SwapchainDesc &desc) override;
  void DestroySwapchain(SwapchainHandle handle) override;
  void ResizeSwapchain(SwapchainHandle handle, u32 width, u32 height) override;

  BufferHandle CreateBuffer(const BufferDesc &desc) override;
  void DestroyBuffer(BufferHandle handle) override;
  const Buffer &GetBuffer(BufferHandle handle) const;
  u64 GetBufferDeviceAddress(BufferHandle handle) const override;

  ImageHandle CreateImage(const ImageDesc &desc) override;
  void DestroyImage(ImageHandle handle) override;
  const Image &GetImage(ImageHandle handle) const;
  void SetImageLayout(ImageHandle handle, u32 baseMipLevel, u32 mipLevelCount,
                      ImageLayout layout);

  ImageViewHandle CreateImageView(const ImageViewDesc &desc) override;
  void DestroyImageView(ImageViewHandle view) override;
  const ImageView &GetImageView(ImageViewHandle handle) const;

  SamplerHandle CreateSampler(const SamplerDesc &desc) override;
  void DestroySampler(SamplerHandle handle) override;
  const Sampler &GetSampler(SamplerHandle handle) const;

  ShaderHandle CreateShader(const ShaderDesc &desc) override;
  void DestroyShader(ShaderHandle handle) override;
  const Shader &GetShader(ShaderHandle handle) const;

  PipelineHandle
  CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) override;
  PipelineHandle
  CreateComputePipeline(const ComputePipelineDesc &desc) override;
  void DestroyPipeline(PipelineHandle handle) override;
  const Pipeline &GetPipeline(PipelineHandle handle) const;

  BindingLayoutHandle
  CreateBindingLayout(const BindingLayoutDesc &desc) override;
  void DestroyBindingLayout(BindingLayoutHandle handle) override;
  const BindingLayout &
  GetBindingLayout(BindingLayoutHandle handle) const;

  BindingPoolHandle
  CreateBindingPool(const BindingPoolDesc &desc) override;
  void DestroyBindingPool(BindingPoolHandle handle) override;
  const BindingPool &
  GetBindingPool(BindingPoolHandle handle) const;
  BindingSetHandle
  AllocateBindingSet(const BindingSetAllocationDesc& desc) override;

  void UpdateBindingSet(const BindingWriteDesc &desc) override;
  const BindingSet &GetBindingSet(BindingSetHandle handle) const;

  ImageLayout GetImageLayout(ImageHandle imageHandle, u32 mipLevel) const;

  FrameBeginResult BeginFrame(SwapchainHandle handle) override;
  ICommandList &GetCommandList() override;
  void Submit() override;
  void SubmitAndPresent(SwapchainHandle swapchain) override;
  void Submit(CommandListHandle handle, VkFence fence);

  void ClearCurrentSwapchainImage(float r, float g, float b, float a);

  void WaitIdle() override;
  void CollectGarbage() override;

  std::unique_ptr<IUploadContext>
  CreateUploadContext(u64 stagingBufferSize = 1024 * 16 * 1024) override;

  void DumpLiveResources() const;

private:
  u32 FindMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) const;

private:
  void CreateInstance(const DeviceDesc &desc);
  void PickPhysicalDevice();
  void CreateLogicalDevice();
  void CreateCommandObjects();
  void CreateSyncObjects();
  void CreateSwapchainSyncObjects();

  void DestroySwapchainSyncObjects();

public:
  VkInstance GetVkInstance() const { return instance_; }
  VkPhysicalDevice GetVkPhysicalDevice() const { return physicalDevice_; }
  VkDevice GetVkDevice() const { return device_; }
  VkQueue GetGraphicsQueue() const { return graphicsQueue_; }
  u32 GetGraphicsQueueFamily() const { return graphicsQueueFamily_; }
  const VkPhysicalDeviceProperties &GetPhysicalDeviceProperties() const {
    return physicalDeviceProperties_;
  }
  VkCommandBuffer GetCommandBuffer() const {
    return commandBuffers_[currentFrame_];
  }

  VkCommandPool GetUploadCommandPool() const { return uploadCommandPool_; }

  Extent2D GetSwapchainDimensions() const override;

  VmaAllocator GetAllocator() { return allocator_; }

private:
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;

  VmaAllocator allocator_ = VK_NULL_HANDLE;

#if VL_PROFILING
  TracyVkCtx tracyContext_ = nullptr;
#endif

  VkPhysicalDeviceProperties physicalDeviceProperties_{};

  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
  u32 graphicsQueueFamily_ = 0;

  VkQueue presentQueue_ = VK_NULL_HANDLE;
  u32 presentQueueFamily_ = 0;

  static constexpr u32 k_MaxFramesInFlight = 2;

  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  VkCommandPool uploadCommandPool_ = VK_NULL_HANDLE;

  std::array<VkCommandBuffer, k_MaxFramesInFlight> commandBuffers_;
  std::array<std::unique_ptr<CommandList>, k_MaxFramesInFlight>
      commandLists_;

  std::unique_ptr<Swapchain> swapchain_;
  std::vector<ImageHandle> swapchainImageHandles_;
  std::vector<ImageViewHandle> swapchainImageViewHandles_;

  u32 currentFrame_ = 0;
  u32 currentBackbufferIndex_ = 0;

  struct FrameSyncData {
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
  };

  std::array<FrameSyncData, k_MaxFramesInFlight> frames_;
  std::vector<VkFence> swapchainImagesInFlight_;
  std::vector<VkSemaphore> swapchainRenderFinishedSemaphores_;

private:
  u32 nextShaderHandle_ = 1;
  std::unordered_map<u32, Shader> shaders_;

  u32 nextPipelineHandle_ = 1;
  std::unordered_map<u32, Pipeline> pipelines_;

  u32 nextBufferHandle_ = 1;
  std::unordered_map<u32, Buffer> buffers_;

  u32 nextImageHandle_ = 1;
  std::unordered_map<u32, Image> images_;

  u32 nextImageViewHandle_ = 1;
  std::unordered_map<u32, ImageView> imageViews_;

  u32 nextSamplerHandle_ = 1;
  std::unordered_map<u32, Sampler> samplers_;

  u32 nextBindingLayoutHandle_ = 1;
  std::unordered_map<u32, BindingLayout> descriptorSetLayouts_;

  u32 nextBindingPoolHandle_ = 1;
  std::unordered_map<u32, BindingPool> descriptorPools_;

  u32 nextBindingSetHandle_ = 1;
  std::unordered_map<u32, BindingSet> descriptorSets_;
};
} // namespace Velos::Vulkan
