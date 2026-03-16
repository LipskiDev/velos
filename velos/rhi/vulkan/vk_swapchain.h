#pragma once

#include "rhi/rhi_device.h"
#include "vk_common.h"

#include <vector>
#include <vulkan/vulkan_core.h>

struct GLFWwindow;

namespace Velos::RHI {
struct VulkanSwapchainImage {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

class VulkanSwapchain {
public:
  VulkanSwapchain(VkInstance instance, VkPhysicalDevice physicalDevice,
                  VkDevice device, u32 graphicsQueueFamily,
                  const SwapchainDesc &desc);
  ~VulkanSwapchain();

  VulkanSwapchain(const VulkanSwapchain &) = delete;
  VulkanSwapchain &operator=(const VulkanSwapchain &) = delete;

  VkSurfaceKHR GetSurface() const { return surface_; }
  VkSwapchainKHR GetVkSwapchain() const { return swapchain_; }
  VkFormat GetFormat() const { return format_; }
  u32 GetWidth() const { return width_; }
  u32 GetHeight() const { return height_; }

  u32 GetImageCount() const { return static_cast<u32>(images_.size()); }
  const VulkanSwapchainImage &GetImage(u32 index) const {
    return images_[index];
  }

private:
  void CreateSurface(void *windowHandle);
  void CreateSwapchain(const SwapchainDesc &desc);
  void CreateImageViews();
  void DestroySwapchainResources();

private:
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  u32 graphicsQueueFamily_ = 0;

  GLFWwindow *window_ = nullptr;

  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat format_ = VK_FORMAT_UNDEFINED;
  u32 width_ = 0;
  u32 height_ = 0;

  std::vector<VkImage> rawImages_;
  std::vector<VulkanSwapchainImage> images_;
};
} // namespace Velos::RHI
