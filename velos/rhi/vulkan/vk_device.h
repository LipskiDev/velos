#pragma once

#include "../rhi_device.h"
#include "vk_command_list.h"
#include "volk.h"
#include <memory>
#include <vulkan/vulkan_core.h>
namespace Velos::RHI {
class VulkanCommandList;

class VulkanDevice final : public IDevice {
public:
  explicit VulkanDevice(const DeviceDesc &desc);
  ~VulkanDevice() override;

  BackendAPI GetBackend() const override;

  SwapchainHandle CreateSwapchain(const SwapchainDesc &desc) override;
  void DestroySwapchain(SwapchainHandle handle) override;
  void ResizeSwapchain(SwapchainHandle handle, u32 width, u32 height) override;

  BufferHandle CreateBuffer(const BufferDesc &desc) override;
  void DestroyBuffer(BufferHandle handle) override;

  TextureHandle CreateTexture(const TextureDesc &desc) override;
  void DestroyTexture(TextureHandle handle) override;

  SamplerHandle CreateSampler(const SamplerDesc &desc) override;
  void DestroySampler(SamplerHandle handle) override;

  ShaderHandle CreateShader(const ShaderDesc &desc) override;
  void DestroyShader(ShaderHandle handle) override;

  PipelineHandle
  CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) override;
  void DestroyPipeline(PipelineHandle handle) override;

  FrameBeginResult BeginFrame(SwapchainHandle handle) override;
  ICommandList &GetCommandList(CommandListHandle handle) override;
  void SubmitAndPresent(CommandListHandle commandList,
                        SwapchainHandle swapchain) override;

  void WaitIdle() override;
  void CollectGarbage() override;

private:
  void CreateInstance(const DeviceDesc &desc);
  void PickPhysicalDevice();
  void CreateLogicalDevice();
  void CreateCommandObjects();

public:
  VkInstance GetVkInstance() const { return instance_; }
  VkPhysicalDevice GetVkPhysicalDevice() const { return physicalDevice_; }
  VkDevice GetVkDevice() const { return device_; }
  VkQueue GetGraphicsQueue() const { return graphicsQueue_; }
  u32 GetGraphicsQueueFamily() const { return graphicsQueueFamily_; }

private:
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;

  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
  u32 graphicsQueueFamily_ = 0;

  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;

  std::unique_ptr<VulkanCommandList> commandList_;
};
} // namespace Velos::RHI
