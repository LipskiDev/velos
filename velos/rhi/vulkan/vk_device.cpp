#include "vk_device.h"
#include "../rhi_device.h"
#include "rhi/rhi_handles.h"
#include "rhi/vulkan/vk_common.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan_core.h>

#include <GLFW/glfw3.h>

namespace Velos::RHI {

VulkanDevice::VulkanDevice(const DeviceDesc &desc) {
  VK_CHECK(volkInitialize(), "Failed to initialize Volk");

  CreateInstance(desc);
  volkLoadInstance(instance_);

  PickPhysicalDevice();
  CreateLogicalDevice();
  volkLoadDevice(device_);

  CreateCommandObjects();
  CreateSyncObjects();
}
VulkanDevice::~VulkanDevice() {
  commandList_.reset();

  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);

    for (auto &[id, pipeline] : pipelines_) {
      if (pipeline.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline.pipeline, nullptr);
        pipeline.pipeline = VK_NULL_HANDLE;
      }
      if (pipeline.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipeline.layout, nullptr);
        pipeline.layout = VK_NULL_HANDLE;
      }
    }
    pipelines_.clear();

    for (ImageViewHandle handle : swapchainImageViewHandles_) {
      DestroyImageView(handle);
    }
    swapchainImageViewHandles_.clear();

    for (ImageHandle handle : swapchainImageHandles_) {
      DestroyImage(handle);
    }
    swapchainImageHandles_.clear();

    DestroySwapchainSyncObjects();
    swapchain_.reset();

    DestroySwapchainSyncObjects();

    if (inFlightFence_ != VK_NULL_HANDLE) {
      vkDestroyFence(device_, inFlightFence_, nullptr);
      inFlightFence_ = VK_NULL_HANDLE;
    }

    if (imageAvailableSemaphore_ != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_, imageAvailableSemaphore_, nullptr);
      imageAvailableSemaphore_ = VK_NULL_HANDLE;
    }

    if (commandPool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device_, commandPool_, nullptr);
      commandPool_ = VK_NULL_HANDLE;
    }

    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }

  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

BackendAPI VulkanDevice::GetBackend() const { return BackendAPI::Vulkan; }

void VulkanDevice::CreateInstance(const DeviceDesc &desc) {
  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = desc.applicationName;
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "Velos";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_3;

  std::vector<const char *> layers = {};
  if (desc.enableValidation) {
    layers.push_back("VK_LAYER_KHRONOS_validation");
  }

  uint32_t glfwExtensionCount = 0;
  const char **glfwExtensions =
      glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
  if (!glfwExtensions) {
    throw std::runtime_error(
        "GLFW did not return required Vulkan instance extensions");
  }

  std::vector<const char *> extensions(glfwExtensions,
                                       glfwExtensions + glfwExtensionCount);

  if (desc.enableValidation) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledLayerCount = static_cast<u32>(layers.size());
  createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
  createInfo.enabledExtensionCount = static_cast<u32>(extensions.size());
  createInfo.ppEnabledExtensionNames =
      extensions.empty() ? nullptr : extensions.data();

  VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance_),
           "Failed to create Vulkan instance");
}

void VulkanDevice::PickPhysicalDevice() {
  u32 deviceCount = 0;
  VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
           "Failed to enumerate physical devices");

  if (deviceCount == 0) {
    throw std::runtime_error("No Vulkan physical devices found");
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()),
           "Failed to enumerate physical devices");

  physicalDevice_ = devices[0];

  VkPhysicalDeviceProperties props{};
  vkGetPhysicalDeviceProperties(physicalDevice_, &props);

  std::cout << "[VulkanDevice] Selected GPU: " << props.deviceName << "\n";

  std::cout << "[VulkanDevice] API version: "
            << VK_VERSION_MAJOR(props.apiVersion) << "."
            << VK_VERSION_MINOR(props.apiVersion) << "."
            << VK_VERSION_PATCH(props.apiVersion) << '\n';
}

void VulkanDevice::CreateLogicalDevice() {
  u32 queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount,
                                           nullptr);

  if (queueFamilyCount == 0) {
    throw std::runtime_error("Physical device exposes no queue families");
  }

  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount,
                                           queueFamilies.data());

  bool foundGraphicsQueue = false;
  for (u32 i = 0; i < queueFamilyCount; ++i) {
    if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      graphicsQueueFamily_ = i;
      foundGraphicsQueue = true;
      break;
    }
  }

  if (!foundGraphicsQueue) {
    throw std::runtime_error("Failed to find graphics queue family");
  }

  const float queuePriority = 1.0f;

  VkDeviceQueueCreateInfo queueCreateInfo{};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = graphicsQueueFamily_;
  queueCreateInfo.queueCount = 1;
  queueCreateInfo.pQueuePriorities = &queuePriority;

  VkPhysicalDeviceFeatures deviceFeatures{};

  VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{};
  dynamicRenderingFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
  dynamicRenderingFeatures.dynamicRendering = VK_TRUE;

  const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.pNext = &dynamicRenderingFeatures;
  createInfo.queueCreateInfoCount = 1;
  createInfo.pQueueCreateInfos = &queueCreateInfo;
  createInfo.pEnabledFeatures = &deviceFeatures;
  createInfo.enabledExtensionCount = 1;
  createInfo.ppEnabledExtensionNames = deviceExtensions;

  VK_CHECK(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
           "Failed to create Vulkan logical device");

  vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);

  presentQueueFamily_ = graphicsQueueFamily_;
  presentQueue_ = graphicsQueue_;
}

void VulkanDevice::CreateCommandObjects() {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = graphicsQueueFamily_;

  VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_),
           "Failed to create Vulkan command pool");

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool_;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VK_CHECK(vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer_),
           "Failed to allocate Vulkan command buffer");

  commandList_ = std::make_unique<VulkanCommandList>(*this, commandBuffer_);
}

void VulkanDevice::CreateSyncObjects() {
  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VK_CHECK(vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                             &imageAvailableSemaphore_),
           "Failed to create image available semaphore");

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  VK_CHECK(vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFence_),
           "Failed to create in-flight fence");
}

void VulkanDevice::CreateSwapchainSyncObjects() {
  if (!swapchain_) {
    throw std::runtime_error(
        "Cannot create swapchain sync objects without a swapchain");
  }

  DestroySwapchainSyncObjects();

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  renderFinishedSemaphores_.resize(swapchain_->GetImageCount(), VK_NULL_HANDLE);

  for (VkSemaphore &semaphore : renderFinishedSemaphores_) {
    VK_CHECK(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore),
             "Failed to create render finished semaphore");
  }
}

void VulkanDevice::DestroySwapchainSyncObjects() {
  for (VkSemaphore semaphore : renderFinishedSemaphores_) {
    if (semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_, semaphore, nullptr);
    }
  }
  renderFinishedSemaphores_.clear();
}

void VulkanDevice::CollectGarbage() {
  // no-op
}

u32 VulkanDevice::FindMemoryType(u32 typeFilter,
                                 VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memProperties{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);

  for (u32 i = 0; i < memProperties.memoryTypeCount; ++i) {
    const bool typeSupported = (typeFilter & (1u << i)) != 0;
    const bool propertyMatch =
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties;

    if (typeSupported && propertyMatch)
      return i;
  }

  throw std::runtime_error("Failed to find suitable memory");
}

void VulkanDevice::TransitionCurrentSwapchainImageForRendering() {
  if (!swapchain_) {
    throw std::runtime_error("No swapchain available");
  }

  const VulkanSwapchainImage &image =
      swapchain_->GetImage(currentBackbufferIndex_);

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image.image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);
}

void VulkanDevice::TransitionCurrentSwapchainImageForPresent() {
  if (!swapchain_) {
    throw std::runtime_error("No swapchain available");
  }

  const VulkanSwapchainImage &image =
      swapchain_->GetImage(currentBackbufferIndex_);

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.dstAccessMask = 0;
  barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image.image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  vkCmdPipelineBarrier(commandBuffer_,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
}

void VulkanDevice::TransitionImageToDepthAttachment(ImageHandle imageHandle) {
  const VulkanImage &img = GetImage(imageHandle);

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  barrier.image = img.image;

  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);
}

SwapchainHandle VulkanDevice::CreateSwapchain(const SwapchainDesc &desc) {
  if (swapchain_) {
    throw std::runtime_error("Only one swapchain is supported for now");
  }

  swapchain_ = std::make_unique<VulkanSwapchain>(
      instance_, physicalDevice_, device_, presentQueueFamily_, desc);

  swapchainImageHandles_.clear();
  swapchainImageViewHandles_.clear();

  for (u32 i = 0; i < swapchain_->GetImageCount(); ++i) {
    const VulkanSwapchainImage &swapImage = swapchain_->GetImage(i);

    VulkanImage wrappedImage{};
    wrappedImage.image = swapImage.image;
    wrappedImage.memory = VK_NULL_HANDLE;
    wrappedImage.format =
        Format::BGRA8_UNORM; // later: derive from swapchain_->GetFormat()
    wrappedImage.usage = ImageUsage::ColorAttachment;
    wrappedImage.layout = ImageLayout::Undefined;
    wrappedImage.width = swapchain_->GetWidth();
    wrappedImage.height = swapchain_->GetHeight();
    wrappedImage.depth = 1;
    wrappedImage.mipLevels = 1;
    wrappedImage.arrayLayers = 1;
    wrappedImage.owned = false;

    const u32 imageHandleId = nextImageHandle_++;
    images_.emplace(imageHandleId, wrappedImage);

    ImageHandle imageHandle{imageHandleId};
    swapchainImageHandles_.push_back(imageHandle);

    VulkanImageView vkView{};
    vkView.view = swapImage.view;
    vkView.image = imageHandle;
    vkView.format = wrappedImage.format;
    vkView.aspect = ImageAspect::Color;
    vkView.owned = false;

    const u32 viewHandleId = nextImageViewHandle_++;
    imageViews_.emplace(viewHandleId, vkView);

    swapchainImageViewHandles_.push_back(ImageViewHandle{viewHandleId});
  }

  CreateSwapchainSyncObjects();

  return SwapchainHandle{1};
}

void VulkanDevice::ClearCurrentSwapchainImage(float r, float g, float b,
                                              float a) {
  if (!swapchain_) {
    throw std::runtime_error("No swapchain available to clear");
  }

  const VulkanSwapchainImage &image =
      swapchain_->GetImage(currentBackbufferIndex_);

  VkImageMemoryBarrier toTransfer{};
  toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toTransfer.srcAccessMask = 0;
  toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.image = image.image;
  toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  toTransfer.subresourceRange.baseMipLevel = 0;
  toTransfer.subresourceRange.levelCount = 1;
  toTransfer.subresourceRange.baseArrayLayer = 0;
  toTransfer.subresourceRange.layerCount = 1;

  vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toTransfer);

  VkClearColorValue clearColor{};
  clearColor.float32[0] = r;
  clearColor.float32[1] = g;
  clearColor.float32[2] = b;
  clearColor.float32[3] = a;

  VkImageSubresourceRange range{};
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.baseMipLevel = 0;
  range.levelCount = 1;
  range.baseArrayLayer = 0;
  range.layerCount = 1;

  vkCmdClearColorImage(commandBuffer_, image.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1,
                       &range);

  VkImageMemoryBarrier toPresent{};
  toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toPresent.dstAccessMask = 0;
  toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toPresent.image = image.image;
  toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  toPresent.subresourceRange.baseMipLevel = 0;
  toPresent.subresourceRange.levelCount = 1;
  toPresent.subresourceRange.baseArrayLayer = 0;
  toPresent.subresourceRange.layerCount = 1;

  vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toPresent);
}

void VulkanDevice::WaitIdle() {
  if (device_ != VK_NULL_HANDLE) {
    VK_CHECK(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle failed");
  }
}

void VulkanDevice::DestroySwapchain(SwapchainHandle handle) {
  if (!handle.IsValid()) {
    return;
  }

  DestroySwapchainSyncObjects();

  for (ImageViewHandle view : swapchainImageViewHandles_) {
    DestroyImageView(view);
  }
  swapchainImageViewHandles_.clear();

  for (ImageHandle image : swapchainImageHandles_) {
    DestroyImage(image);
  }
  swapchainImageHandles_.clear();

  swapchain_.reset();
}

void VulkanDevice::ResizeSwapchain(SwapchainHandle, u32, u32) {
  throw std::runtime_error("ResizeSwapchain not implemented yet");
}

BufferHandle VulkanDevice::CreateBuffer(const BufferDesc &desc) {

  if (desc.size == 0) {
    throw std::runtime_error(
        "CreateBuffer: buffer size must be greater than 0");
  }

  if (desc.usage == BufferUsage::None) {
    throw std::runtime_error("CreateBuffer: buffer usage must not be None");
  }

  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = desc.size;
  bufferInfo.usage = ToVkBufferUsageFlags(desc.usage);
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VulkanBuffer buffer{};
  buffer.size = desc.size;
  buffer.usage = desc.usage;
  buffer.memoryUsage = desc.memoryUsage;

  VkResult result =
      vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer.buffer);
  if (result != VK_SUCCESS) {
    throw std::runtime_error("CreateBuffer: vkCreateBuffer failed");
  }

  VkMemoryRequirements memRequirements{};
  vkGetBufferMemoryRequirements(device_, buffer.buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      FindMemoryType(memRequirements.memoryTypeBits,
                     ToVkMemoryPropertyFlags(desc.memoryUsage));

  result = vkAllocateMemory(device_, &allocInfo, nullptr, &buffer.memory);

  if (result != VK_SUCCESS) {
    vkDestroyBuffer(device_, buffer.buffer, nullptr);
    throw std::runtime_error("CreateBuffer: vkAllocateMemory failed");
  }

  result = vkBindBufferMemory(device_, buffer.buffer, buffer.memory, 0);

  if (result != VK_SUCCESS) {
    vkFreeMemory(device_, buffer.memory, nullptr);
    vkDestroyBuffer(device_, buffer.buffer, nullptr);
    throw std::runtime_error("CreateBuffer: vkBindBufferMemory failed");
  }

  if (desc.initialData != nullptr) {
    if (desc.memoryUsage == MemoryUsage::GPUOnly) {
      vkFreeMemory(device_, buffer.memory, nullptr);
      vkDestroyBuffer(device_, buffer.buffer, nullptr);
      throw std::runtime_error("CreateBuffer: initialData for GPUOnly buffers "
                               "requires staging upload path");
    }

    void *mappedData = nullptr;
    result = vkMapMemory(device_, buffer.memory, 0, desc.size, 0, &mappedData);

    if (result != VK_SUCCESS || mappedData == nullptr) {
      vkFreeMemory(device_, buffer.memory, nullptr);
      vkDestroyBuffer(device_, buffer.buffer, nullptr);
      throw std::runtime_error("CreateBuffer: vkMapMemory failed");
    }

    memcpy(mappedData, desc.initialData, static_cast<size_t>(desc.size));

    if ((ToVkMemoryPropertyFlags(desc.memoryUsage) &
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
      VkMappedMemoryRange range{};
      range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      range.memory = buffer.memory;
      range.offset = 0;
      range.size = desc.size;
      vkFlushMappedMemoryRanges(device_, 1, &range);
    }

    vkUnmapMemory(device_, buffer.memory);
  }

  const u32 handleId = nextBufferHandle_++;
  buffers_.emplace(handleId, buffer);

  return BufferHandle{handleId};
}

void VulkanDevice::DestroyBuffer(BufferHandle handle) {
  auto it = buffers_.find(handle.id);
  if (it == buffers_.end()) {
    return;
  }

  VulkanBuffer buffer = it->second;

  if (buffer.buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, buffer.buffer, nullptr);
    buffer.buffer = VK_NULL_HANDLE;
  }

  if (buffer.memory != VK_NULL_HANDLE) {
    vkFreeMemory(device_, buffer.memory, nullptr);
    buffer.memory = VK_NULL_HANDLE;
  }

  buffers_.erase(it);
}

const VulkanBuffer &VulkanDevice::GetBuffer(BufferHandle handle) const {
  auto it = buffers_.find(handle.id);

  if (it == buffers_.end()) {
    throw std::runtime_error("Invalid texture handle");
  }

  return it->second;
}

ImageHandle VulkanDevice::CreateImage(const ImageDesc &desc) {

  if (desc.width == 0 || desc.height == 0 || desc.depth == 0) {
    throw std::runtime_error(
        "CreateImage: image dimensions must be greater than 0");
  }

  if (desc.format == Format::Undefined) {
    throw std::runtime_error("CreateImage: format must not be Undefined");
  }

  if (desc.usage == ImageUsage::None) {
    throw std::runtime_error("CreateImage: usage must not be None");
  }

  VkImageCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  createInfo.imageType = VK_IMAGE_TYPE_2D;
  createInfo.format = ToVkFormat(desc.format);
  createInfo.extent = {desc.width, desc.height, desc.depth};
  createInfo.mipLevels = desc.mipLevels;
  createInfo.arrayLayers = desc.arrayLayers;
  createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  createInfo.usage = ToVkImageUsage(desc.usage);
  createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage image = VK_NULL_HANDLE;
  VkResult result = vkCreateImage(device_, &createInfo, nullptr, &image);
  if (result != VK_SUCCESS) {
    throw std::runtime_error("CreateImage: vkCreateImage failed");
  }

  VkMemoryRequirements memRequirements{};
  vkGetImageMemoryRequirements(device_, image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = FindMemoryType(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  VkDeviceMemory memory = VK_NULL_HANDLE;
  result = vkAllocateMemory(device_, &allocInfo, nullptr, &memory);
  if (result != VK_SUCCESS) {
    vkDestroyImage(device_, image, nullptr);
    throw std::runtime_error("CreateImage: vkAllocateMemory failed");
  }

  result = vkBindImageMemory(device_, image, memory, 0);
  if (result != VK_SUCCESS) {
    vkFreeMemory(device_, memory, nullptr);
    vkDestroyImage(device_, image, nullptr);
    throw std::runtime_error("CreateImage: vkBindImageMemory failed");
  }

  VulkanImage vkImage{};
  vkImage.image = image;
  vkImage.memory = memory;
  vkImage.format = desc.format;
  vkImage.usage = desc.usage;
  vkImage.layout = ImageLayout::Undefined;
  vkImage.width = desc.width;
  vkImage.height = desc.height;
  vkImage.depth = desc.depth;
  vkImage.mipLevels = desc.mipLevels;
  vkImage.arrayLayers = desc.arrayLayers;
  vkImage.owned = true;

  const u32 handleId = nextImageHandle_++;
  images_.emplace(handleId, vkImage);

  return ImageHandle{handleId};
}
void VulkanDevice::DestroyImage(ImageHandle handle) {
  if (!handle.IsValid()) {
    return;
  }

  auto it = images_.find(handle.id);
  if (it == images_.end()) {
    return;
  }

  VulkanImage &image = it->second;

  if (image.image != VK_NULL_HANDLE && image.owned) {
    vkDestroyImage(device_, image.image, nullptr);
    image.image = VK_NULL_HANDLE;
  }

  if (image.memory != VK_NULL_HANDLE) {
    vkFreeMemory(device_, image.memory, nullptr);
    image.memory = VK_NULL_HANDLE;
  }

  images_.erase(it);
}

const VulkanImage &VulkanDevice::GetImage(ImageHandle handle) const {

  auto it = images_.find(handle.id);
  if (it == images_.end()) {
    throw std::runtime_error("Invalid image handle");
  }

  return it->second;
}

ImageViewHandle VulkanDevice::CreateImageView(const ImageViewDesc &desc) {
  if (!desc.image.IsValid()) {
    throw std::runtime_error("CreateImageView: invalid image handle");
  }

  const VulkanImage &vkImage = GetImage(desc.image);

  if (vkImage.image == VK_NULL_HANDLE) {
    throw std::runtime_error("CreateImageView: source image is null");
  }

  Format viewFormat =
      desc.format == Format::Undefined ? vkImage.format : desc.format;
  ImageAspect aspect = desc.aspect == ImageAspect::None
                           ? DefaultImageAspectFromFormat(viewFormat)
                           : desc.aspect;

  if (aspect == ImageAspect::None) {
    throw std::runtime_error(
        "CreateImageView: could not determine image aspect");
  }

  if (desc.mipLevelCount == 0) {
    throw std::runtime_error(
        "CreateImageView: mipLevelCount must be greater than 0");
  }

  if (desc.arrayLayerCount == 0) {
    throw std::runtime_error(
        "CreateImageView: arrayLayerCount must be greater than 0");
  }

  if (desc.baseMipLevel + desc.mipLevelCount > vkImage.mipLevels) {
    throw std::runtime_error("CreateImageView: mip range out of bounds");
  }

  if (desc.baseArrayLayer + desc.arrayLayerCount > vkImage.arrayLayers) {
    throw std::runtime_error(
        "CreateImageView: array layer range out of bounds");
  }

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = vkImage.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = ToVkFormat(viewFormat);
  viewInfo.subresourceRange.aspectMask = ToVkImageAspect(aspect);
  viewInfo.subresourceRange.baseMipLevel = desc.baseMipLevel;
  viewInfo.subresourceRange.levelCount = desc.mipLevelCount;
  viewInfo.subresourceRange.baseArrayLayer = desc.baseArrayLayer;
  viewInfo.subresourceRange.layerCount = desc.arrayLayerCount;

  VkImageView view = VK_NULL_HANDLE;
  VkResult result = vkCreateImageView(device_, &viewInfo, nullptr, &view);
  if (result != VK_SUCCESS) {
    throw std::runtime_error("CreateImageView: vkCreateImageView failed");
  }

  VulkanImageView vkView{};
  vkView.view = view;
  vkView.image = desc.image;
  vkView.format = viewFormat;
  vkView.aspect = aspect;

  u32 handleId = nextImageViewHandle_++;

  imageViews_.emplace(handleId, vkView);

  return ImageViewHandle{handleId};
}

void VulkanDevice::DestroyImageView(ImageViewHandle handle) {
  if (!handle.IsValid()) {
    return;
  }

  auto it = imageViews_.find(handle.id);

  if (it == imageViews_.end()) {
    return;
  }

  VulkanImageView &view = it->second;

  if (view.view != VK_NULL_HANDLE && view.owned) {
    vkDestroyImageView(device_, view.view, nullptr);
  }
  view.view = VK_NULL_HANDLE;

  imageViews_.erase(it);
}

const VulkanImageView &
VulkanDevice::GetImageView(ImageViewHandle handle) const {
  auto it = imageViews_.find(handle.id);
  if (it == imageViews_.end()) {
    throw std::runtime_error("Invalid image view handle");
  }

  return it->second;
}

SamplerHandle VulkanDevice::CreateSampler(const SamplerDesc &) {
  throw std::runtime_error("CreateSampler not implemented yet");
}

void VulkanDevice::DestroySampler(SamplerHandle) {
  throw std::runtime_error("DestroySampler not implemented yet");
}

ShaderHandle VulkanDevice::CreateShader(const ShaderDesc &desc) {
  if (!desc.bytecode) {
    throw std::runtime_error("CreateShader called with null bytecode");
  }

  if (desc.bytecodeSize == 0) {
    throw std::runtime_error("CreateShader called with empty bytecode");
  }

  if ((desc.bytecodeSize % 4) != 0) {
    throw std::runtime_error(
        "CreateShader bytecode size must be a multiple of 4");
  }

  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = static_cast<size_t>(desc.bytecodeSize);
  createInfo.pCode = static_cast<const std::uint32_t *>(desc.bytecode);

  VkShaderModule shaderModule = VK_NULL_HANDLE;
  VK_CHECK(vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule),
           "Failed to create Vulkan shader module");

  const u32 handleId = nextShaderHandle_++;
  shaders_.emplace(handleId, VulkanShader{.module = shaderModule,
                                          .stage = desc.stage,
                                          .reflection = desc.reflection});

  return ShaderHandle{handleId};
}

void VulkanDevice::DestroyShader(ShaderHandle handle) {
  if (!handle.IsValid()) {
    return;
  }

  auto it = shaders_.find(handle.id);
  if (it == shaders_.end()) {
    return;
  }

  if (it->second.module != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device_, it->second.module, nullptr);
  }

  shaders_.erase(it);
}

const VulkanShader &VulkanDevice::GetShader(ShaderHandle handle) const {
  auto it = shaders_.find(handle.id);
  if (it == shaders_.end()) {
    throw std::runtime_error("Invalid shader handle");
  }

  return it->second;
}

PipelineHandle
VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) {
  if (!desc.vertexShader.IsValid()) {
    throw std::runtime_error(
        "CreateGraphicsPipeline requires a valid vertex shader");
  }

  if (!desc.fragmentShader.IsValid()) {
    throw std::runtime_error(
        "CreateGraphicsPipeline requires a valid fragment shader");
  }

  const VulkanShader &vs = GetShader(desc.vertexShader);
  const VulkanShader &fs = GetShader(desc.fragmentShader);

  VkPipelineShaderStageCreateInfo shaderStages[2]{};

  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = vs.module;
  shaderStages[0].pName = "main";

  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = fs.module;
  shaderStages[1].pName = "main";

  std::vector<VkVertexInputBindingDescription> bindings;
  bindings.reserve(desc.vertexLayouts.size());

  for (u32 i = 0; i < desc.vertexLayouts.size(); ++i) {
    const auto &layout = desc.vertexLayouts[i];

    VkVertexInputBindingDescription binding{};
    binding.binding = i;
    binding.stride = layout.stride;
    binding.inputRate = ToVkInputRate(layout.inputRate);

    bindings.push_back(binding);
  }

  std::vector<VkVertexInputAttributeDescription> attributes;

  for (u32 bindingIndex = 0; bindingIndex < desc.vertexLayouts.size();
       ++bindingIndex) {

    const auto &layout = desc.vertexLayouts[bindingIndex];

    for (const auto &attr : layout.attributes) {
      VkVertexInputAttributeDescription vkAttr{};
      vkAttr.location = attr.location;
      vkAttr.binding = bindingIndex;
      vkAttr.format = ToVkVertexFormat(attr.format);
      vkAttr.offset = attr.offset;

      attributes.push_back(vkAttr);
    }
  }

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount =
      static_cast<u32>(bindings.size());
  vertexInputInfo.pVertexBindingDescriptions = bindings.data();
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<u32>(attributes.size());
  vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = ToVkPrimitiveTopology(desc.topology);
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode =
      desc.raster.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode =
      desc.raster.cullBackFaces ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
  rasterizer.frontFace = desc.raster.frontFaceCCW
                             ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                             : VK_FRONT_FACE_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.blendEnable = desc.blend.enable ? VK_TRUE : VK_FALSE;
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                    VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  std::vector<VkPushConstantRange> vertexPushConstantRanges = {};
  vertexPushConstantRanges.reserve(vs.reflection.pushConstants.size() +
                                   fs.reflection.pushConstants.size());

  for (auto &pushConstantRange : vs.reflection.pushConstants) {
    VkPushConstantRange pcr;
    pcr.size = pushConstantRange.size;
    pcr.offset = pushConstantRange.offset;
    pcr.stageFlags = ToVkShaderStage(ShaderStage::Vertex);
    vertexPushConstantRanges.push_back(pcr);
  }

  for (auto &pushConstantRange : fs.reflection.pushConstants) {
    VkPushConstantRange pcr;
    pcr.size = pushConstantRange.size;
    pcr.offset = pushConstantRange.offset;
    pcr.stageFlags = ToVkShaderStage(ShaderStage::Fragment);
    vertexPushConstantRanges.push_back(pcr);
  }

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 0;
  pipelineLayoutInfo.pSetLayouts = nullptr;
  pipelineLayoutInfo.pushConstantRangeCount = vertexPushConstantRanges.size();
  pipelineLayoutInfo.pPushConstantRanges = vertexPushConstantRanges.data();

  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VK_CHECK(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr,
                                  &pipelineLayout),
           "Failed to create Vulkan pipeline layout");

  VkFormat colorFormat = ToVkFormat(desc.colorFormat);
  if (colorFormat == VK_FORMAT_UNDEFINED) {
    vkDestroyPipelineLayout(device_, pipelineLayout, nullptr);
    throw std::runtime_error(
        "CreateGraphicsPipeline received unsupported color format");
  }

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable =
      desc.depth.depthTestEnable ? VK_TRUE : VK_FALSE;
  depthStencil.depthWriteEnable =
      desc.depth.depthWriteEnable ? VK_TRUE : VK_FALSE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  VkPipelineRenderingCreateInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachmentFormats = &colorFormat;
  renderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
  renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.pNext = &renderingInfo;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.renderPass = VK_NULL_HANDLE;
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

  VkPipeline pipeline = VK_NULL_HANDLE;
  VkResult result = vkCreateGraphicsPipelines(
      device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

  if (result != VK_SUCCESS) {
    vkDestroyPipelineLayout(device_, pipelineLayout, nullptr);
    throw std::runtime_error("Failed to create Vulkan graphics pipeline");
  }

  const u32 handleId = nextPipelineHandle_++;
  pipelines_.emplace(
      handleId, VulkanPipeline{.pipeline = pipeline, .layout = pipelineLayout});

  return PipelineHandle{handleId};
}

void VulkanDevice::DestroyPipeline(PipelineHandle handle) {
  if (!handle.IsValid()) {
    return;
  }

  auto it = pipelines_.find(handle.id);
  if (it == pipelines_.end()) {
    return;
  }

  if (it->second.pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device_, it->second.pipeline, nullptr);
    it->second.pipeline = VK_NULL_HANDLE;
  }

  if (it->second.layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device_, it->second.layout, nullptr);
    it->second.layout = VK_NULL_HANDLE;
  }

  pipelines_.erase(it);
}

const VulkanPipeline &VulkanDevice::GetPipeline(PipelineHandle handle) const {
  auto it = pipelines_.find(handle.id);
  if (it == pipelines_.end()) {
    throw std::runtime_error("Invalid shader handle");
  }

  return it->second;
}

FrameBeginResult VulkanDevice::BeginFrame(SwapchainHandle swapchain) {
  if (!swapchain.IsValid()) {
    throw std::runtime_error("BeginFrame called with invalid swapchain handle");
  }

  if (!swapchain_) {
    throw std::runtime_error("BeginFrame called without a created swapchain");
  }

  VK_CHECK(vkWaitForFences(device_, 1, &inFlightFence_, VK_TRUE, UINT64_MAX),
           "Failed to wait for in-flight fence");

  VK_CHECK(vkResetFences(device_, 1, &inFlightFence_),
           "Failed to reset in-flight fence");

  u32 imageIndex = 0;
  VkResult result = vkAcquireNextImageKHR(device_, swapchain_->GetVkSwapchain(),
                                          UINT64_MAX, imageAvailableSemaphore_,
                                          VK_NULL_HANDLE, &imageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return FrameBeginResult({
        .commandList = CommandListHandle{},
        .backbuffer = ImageViewHandle{},
        .backbufferIndex = 0,
        .success = false,
    });
  }

  if (result == VK_SUBOPTIMAL_KHR) {
    currentBackbufferIndex_ = imageIndex;

    return FrameBeginResult{.commandList = CommandListHandle{1},
                            .backbuffer =
                                swapchainImageViewHandles_[imageIndex],
                            .backbufferIndex = imageIndex,
                            .success = true};
  }

  VK_CHECK(result, "Failed to acquire next swapchain image");

  currentBackbufferIndex_ = imageIndex;

  return FrameBeginResult{.commandList = CommandListHandle{1},
                          .backbuffer = swapchainImageViewHandles_[imageIndex],
                          .backbufferIndex = imageIndex,
                          .success = true};
}

ICommandList &VulkanDevice::GetCommandList(CommandListHandle handle) {
  if (!handle.IsValid()) {
    throw std::runtime_error("Invalid command list handle");
  }

  if (!commandList_) {
    throw std::runtime_error("Command list has not been created");
  }

  return *commandList_;
}

void VulkanDevice::SubmitAndPresent(CommandListHandle commandList,
                                    SwapchainHandle swapchain) {
  if (!commandList.IsValid()) {
    throw std::runtime_error(
        "SubmitAndPresent called with invalid command list handle");
  }

  if (!swapchain.IsValid()) {
    throw std::runtime_error(
        "SubmitAndPresent called with invalid swapchain handle");
  }

  if (!swapchain_) {
    throw std::runtime_error(
        "SubmitAndPresent called without a created swapchain");
  }

  if (currentBackbufferIndex_ >= renderFinishedSemaphores_.size()) {
    throw std::runtime_error("Current backbuffer index is out of range");
  }

  VkSemaphore renderFinishedSemaphore =
      renderFinishedSemaphores_[currentBackbufferIndex_];

  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &imageAvailableSemaphore_;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer_;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

  VK_CHECK(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFence_),
           "Failed to submit Vulkan command buffer");

  VkSwapchainKHR swapchains[] = {swapchain_->GetVkSwapchain()};

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapchains;
  presentInfo.pImageIndices = &currentBackbufferIndex_;

  VkResult result = vkQueuePresentKHR(presentQueue_, &presentInfo);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    return;
  }

  VK_CHECK(result, "Failed to present Vulkan swapchain image");
}
} // namespace Velos::RHI
