#include "rhi/vulkan/vk_swapchain.h"
#include "rhi/rhi_device.h"
#include "rhi/vulkan/vk_common.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <format>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace Velos::RHI {

namespace {

VkSurfaceFormatKHR
ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &formats) {
  for (const auto &format : formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return format;
    }
  }

  return formats[0];
}

VkPresentModeKHR
ChoosePresentMode(const std::vector<VkPresentModeKHR> &presentModes,
                  bool vsync) {
  if (vsync) {
    return VK_PRESENT_MODE_FIFO_KHR;
  }

  for (VkPresentModeKHR mode : presentModes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return mode;
    }
  }

  for (VkPresentModeKHR mode : presentModes) {
    if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
      return mode;
    }
  }

  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR &capabilities, u32 width,
                        u32 height) {
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }

  VkExtent2D extent{};
  extent.width = std::clamp(width, capabilities.minImageExtent.width,
                            capabilities.maxImageExtent.width);
  extent.height = std::clamp(height, capabilities.minImageExtent.height,
                             capabilities.maxImageExtent.height);
  return extent;
}

} // namespace
VulkanSwapchain::VulkanSwapchain(VkInstance instance,
                                 VkPhysicalDevice physicalDevice,
                                 VkDevice device, u32 graphicsQueueFamily,
                                 const SwapchainDesc &desc)
    : instance_(instance), physicalDevice_(physicalDevice), device_(device),
      graphicsQueueFamily_(graphicsQueueFamily), width_(desc.width),
      height_(desc.height), desc_(desc) {
  std::cout << "Creating Surface\n";
  CreateSurface(desc.windowHandle);

  std::cout << "Creating Swapchain\n";
  CreateSwapchain(desc);

  std::cout << "Create Image views\n";
  CreateImageViews();
}

VulkanSwapchain::~VulkanSwapchain() {
  DestroySwapchainResources();

  if (surface_ != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }
}

void VulkanSwapchain::CreateSurface(void *windowHandle) {
  window_ = static_cast<GLFWwindow *>(windowHandle);
  if (!window_) {
    throw std::runtime_error("Swapchain requires a valid GLFW window handle");
  }

  VK_CHECK(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
           "Failed to create GLFW Vulkan surface");
}

void VulkanSwapchain::CreateSwapchain(const SwapchainDesc &desc) {
  VkBool32 presentSupported = VK_FALSE;
  VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_,
                                                graphicsQueueFamily_, surface_,
                                                &presentSupported),
           "Failed to query surface support");

  if (presentSupported != VK_TRUE) {
    throw std::runtime_error(
        "Selected graphics queue family does not support present");
  }

  VkSurfaceCapabilitiesKHR capabilities{};
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_,
                                                     &capabilities),
           "Failed to query surface capabilities");

  u32 formatCount = 0;
  VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_,
                                                &formatCount, nullptr),
           "Failed to query surface format count");

  if (formatCount == 0) {
    throw std::runtime_error("Surface reports no supported formats");
  }

  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_,
                                                &formatCount, formats.data()),
           "Failed to query surface formats");

  u32 presentModeCount = 0;
  VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
               physicalDevice_, surface_, &presentModeCount, nullptr),
           "Failed to query present mode count");

  if (presentModeCount == 0) {
    throw std::runtime_error("Surface reports no supported present modes");
  }

  std::vector<VkPresentModeKHR> presentModes(presentModeCount);
  VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_,
                                                     &presentModeCount,
                                                     presentModes.data()),
           "Failed to query present modes");

  const VkSurfaceFormatKHR chosenFormat = ChooseSurfaceFormat(formats);
  const VkPresentModeKHR chosenPresentMode =
      ChoosePresentMode(presentModes, desc.vsync);
  const VkExtent2D chosenExtent =
      ChooseExtent(capabilities, desc.width, desc.height);
  auto PresentModeToString = [](VkPresentModeKHR mode) {
    switch (mode) {
    case VK_PRESENT_MODE_FIFO_KHR:
      return "FIFO (vsync ON)";
    case VK_PRESENT_MODE_MAILBOX_KHR:
      return "MAILBOX (triple buffering)";
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
      return "IMMEDIATE (no vsync)";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
      return "FIFO_RELAXED";
    default:
      return "UNKNOWN";
    }
  };

  std::cout << "[Swapchain] Present mode: "
            << PresentModeToString(chosenPresentMode) << std::endl;
  u32 imageCount = capabilities.minImageCount + 2;
  if (capabilities.maxImageCount > 0 &&
      imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }

  format_ = chosenFormat.format;
  width_ = chosenExtent.width;
  height_ = chosenExtent.height;

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = surface_;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = chosenFormat.format;
  createInfo.imageColorSpace = chosenFormat.colorSpace;
  createInfo.imageExtent = chosenExtent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  createInfo.preTransform = capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = chosenPresentMode;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;

  VK_CHECK(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_),
           "Failed to create Vulkan swapchain");

  u32 swapchainImageCount = 0;
  VK_CHECK(vkGetSwapchainImagesKHR(device_, swapchain_, &swapchainImageCount,
                                   nullptr),
           "Failed to query swapchain image count");

  rawImages_.resize(swapchainImageCount);
  VK_CHECK(vkGetSwapchainImagesKHR(device_, swapchain_, &swapchainImageCount,
                                   rawImages_.data()),
           "Failed to get swapchain images");
}

void VulkanSwapchain::CreateImageViews() {
  images_.clear();
  images_.reserve(rawImages_.size());

  for (VkImage rawImage : rawImages_) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = rawImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(device_, &viewInfo, nullptr, &view),
             "Failed to create swapchain image view");

    VulkanSwapchainImage swapImage{};
    swapImage.image = rawImage;
    swapImage.view = view;

    images_.push_back(swapImage);
  }
}

void VulkanSwapchain::DestroySwapchainResources() {
  for (auto &image : images_) {
    if (image.view != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, image.view, nullptr);
      image.view = VK_NULL_HANDLE;
    }
  }

  images_.clear();
  rawImages_.clear();

  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

} // namespace Velos::RHI
