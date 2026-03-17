#pragma once

#include "../rhi_device.h"
#include "core/types.h"
#include "rhi/rhi_handles.h"
#include "rhi/rhi_types.h"
#include "vk_command_list.h"
#include "vk_swapchain.h"
#include "volk.h"
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

namespace Velos::RHI {
class VulkanCommandList;

struct VulkanShader {
  VkShaderModule module = VK_NULL_HANDLE;
  ShaderStage stage = ShaderStage::None;
};

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
  const VulkanShader &GetShader(ShaderHandle handle) const;

  PipelineHandle
  CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) override;
  void DestroyPipeline(PipelineHandle handle) override;

  FrameBeginResult BeginFrame(SwapchainHandle handle) override;
  ICommandList &GetCommandList(CommandListHandle handle) override;
  void SubmitAndPresent(CommandListHandle commandList,
                        SwapchainHandle swapchain) override;

  void ClearCurrentSwapchainImage(float r, float g, float b, float a);

  void WaitIdle() override;
  void CollectGarbage() override;

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

private:
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;

  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
  u32 graphicsQueueFamily_ = 0;

  VkQueue presentQueue_ = VK_NULL_HANDLE;
  u32 presentQueueFamily_ = 0;

  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;

  std::unique_ptr<VulkanCommandList> commandList_;
  std::unique_ptr<VulkanSwapchain> swapchain_;

  VkSemaphore imageAvailableSemaphore_;
  std::vector<VkSemaphore> renderFinishedSemaphores_;
  VkFence inFlightFence_;
  u32 currentBackbufferIndex_;

private:
  u32 nextShaderHandle_ = 1;
  std::unordered_map<u32, VulkanShader> shaders_;
};
} // namespace Velos::RHI
