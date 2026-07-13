#include "device.h"
#include "../device.h"
#include "rhi/handles.h"
#include "rhi/resources.h"
#include "rhi/types.h"
#include "rhi/vulkan/command_list.h"
#include "rhi/vulkan/common.h"
#include "rhi/vulkan/profiler.h"
#include "rhi/vulkan/upload_context.h"
#include "vlpch.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <vk_mem_alloc.h>

#include <GLFW/glfw3.h>

#include <core/profiling.h>
#include <vulkan/vulkan_core.h>

namespace Velos::Vulkan {
using namespace Velos::RHI;

static const char *BoolStr(bool v) { return v ? "true" : "false"; }

void Device::DumpLiveResources() const {
  std::cout << "\n==== Live Device resources ====\n";

  std::cout << "Buffers: " << buffers_.size() << "\n";
  for (const auto &[id, buffer] : buffers_) {
    std::cout << "  Buffer id=" << id << " vkBuffer=" << buffer.buffer
              << " allocation=" << buffer.allocation << " size=" << buffer.size
              << " usage=" << static_cast<u32>(buffer.usage)
              << " memoryUsage=" << static_cast<u32>(buffer.memoryUsage)
              << "\n";
  }

  std::cout << "Images: " << images_.size() << "\n";
  for (const auto &[id, image] : images_) {
    std::cout << "  Image id=" << id << " vkImage=" << image.image
              << " memory=" << image.memory << " owned=" << BoolStr(image.owned)
              << " size=" << image.width << "x" << image.height << "x"
              << image.depth << " layers=" << image.arrayLayers
              << " mips=" << image.mipLevels << "\n";
  }

  std::cout << "ImageViews: " << imageViews_.size() << "\n";
  for (const auto &[id, view] : imageViews_) {
    std::cout << "  ImageView id=" << id << " vkView=" << view.view
              << " imageHandle=" << view.image.id
              << " owned=" << BoolStr(view.owned) << "\n";
  }

  std::cout << "Samplers: " << samplers_.size() << "\n";
  for (const auto &[id, sampler] : samplers_) {
    std::cout << "  Sampler id=" << id << " vkSampler=" << sampler.sampler
              << "\n";
  }

  std::cout << "Shaders: " << shaders_.size() << "\n";
  for (const auto &[id, shader] : shaders_) {
    std::cout << "  Shader id=" << id << " module=" << shader.module
              << " stage=" << static_cast<u32>(shader.stage) << "\n";
  }

  std::cout << "Pipelines: " << pipelines_.size() << "\n";
  std::cout << "DescriptorSetLayouts: " << descriptorSetLayouts_.size() << "\n";
  std::cout << "DescriptorPools: " << descriptorPools_.size() << "\n";
  std::cout << "DescriptorSets: " << descriptorSets_.size() << "\n";

  std::cout << "=====================================\n";
}

Device::Device(const DeviceDesc &desc) {
  VL_PROFILE_ZONE_N("Device::Device");

  VK_CHECK(volkInitialize(), "Failed to initialize Volk");

  CreateInstance(desc);
  volkLoadInstance(instance_);

  PickPhysicalDevice();
  CreateLogicalDevice();
  volkLoadDevice(device_);

  CreateAllocator();

  CreateCommandObjects();
  CreateSyncObjects();
}

Device::~Device() {
  VL_PROFILE_ZONE_N("Device::~Device");

  for (auto &commandList : commandLists_) {
    commandList.reset();
  }

#if VL_PROFILING
  if (tracyContext_) {
    VL_PROFILE_GPU_DESTROY(tracyContext_);
    tracyContext_ = nullptr;
  }
#endif

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

    for (u32 i = 0; i < k_MaxFramesInFlight; ++i) {
      if (frames_[i].inFlightFence != VK_NULL_HANDLE) {
        vkDestroyFence(device_, frames_[i].inFlightFence, nullptr);
        frames_[i].inFlightFence = VK_NULL_HANDLE;
      }

      if (frames_[i].imageAvailableSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, frames_[i].imageAvailableSemaphore,
                           nullptr);
        frames_[i].imageAvailableSemaphore = VK_NULL_HANDLE;
      }
    }

    if (commandPool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device_, commandPool_, nullptr);
      commandPool_ = VK_NULL_HANDLE;
    }

    if (uploadCommandPool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device_, uploadCommandPool_, nullptr);
      uploadCommandPool_ = VK_NULL_HANDLE;
    }

    DumpLiveResources();

    if (allocator_ != VK_NULL_HANDLE) {
      vmaDestroyAllocator(allocator_);
      allocator_ = VK_NULL_HANDLE;
    }

    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }

  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

void Device::CreateAllocator() {
  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.instance = instance_;
  allocatorInfo.physicalDevice = physicalDevice_;
  allocatorInfo.device = device_;
  allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
  allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

  VmaVulkanFunctions vmaFunctions{};
  VK_CHECK(vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vmaFunctions),
           "Failed to import Vulkan functions from Volk");

  allocatorInfo.pVulkanFunctions = &vmaFunctions;

  VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator_),
           "Failed to create VMA allocator");

  if (allocator_ == VK_NULL_HANDLE) {
    throw std::runtime_error(
        "CreateAllocator: allocator_ is null after creation");
  }
}
GraphicsAPI Device::GetBackend() const { return GraphicsAPI::Vulkan; }

void Device::CreateInstance(const DeviceDesc &desc) {
  VL_PROFILE_ZONE_N("Device::CreateInstance");
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

  std::cout << "GLFW Vulkan instance extensions:\n";
  for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
    std::cout << "  " << glfwExtensions[i] << "\n";
  }

  std::vector<const char *> extensions(glfwExtensions,
                                       glfwExtensions + glfwExtensionCount);

  if (desc.enableValidation) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  std::cout << "Enabled Vulkan layers:\n";
  for (const char *layer : layers) {
    std::cout << "  " << layer << "\n";
  }

  std::cout << "Enabled Vulkan instance extensions:\n";
  for (const char *ext : extensions) {
    std::cout << "  " << ext << "\n";
  }

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledLayerCount = static_cast<u32>(layers.size());
  createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
  createInfo.enabledExtensionCount = static_cast<u32>(extensions.size());
  createInfo.ppEnabledExtensionNames =
      extensions.empty() ? nullptr : extensions.data();

  VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
  std::cout << "vkCreateInstance result = " << result << "\n";
  VK_CHECK(result, "Failed to create Vulkan instance");
}

bool HasRequiredQueues(VkPhysicalDevice device) {
  u32 queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

  if (queueFamilyCount == 0) {
    return false;
  }

  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                           queueFamilies.data());

  for (const auto &queueFamily : queueFamilies) {
    const bool hasGraphics =
        (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    const bool hasCompute =
        (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;

    if (queueFamily.queueCount > 0 && hasGraphics && hasCompute) {
      return true;
    }
  }

  return false;
}

bool HasRequiredExtensions(VkPhysicalDevice device) {
  u32 extensionCount = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       nullptr);

  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       availableExtensions.data());

  const char *requiredExtensions[] = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };

  for (const char *required : requiredExtensions) {
    bool found = false;

    for (const auto &extension : availableExtensions) {
      if (strcmp(extension.extensionName, required) == 0) {
        found = true;
        break;
      }
    }

    if (!found) {
      return false;
    }
  }

  return true;
}

bool HasRequiredFeatures(VkPhysicalDevice device) {
  VkPhysicalDeviceFeatures features{};
  VkPhysicalDeviceBufferDeviceAddressFeatures bda{};
  VkPhysicalDeviceDescriptorIndexingFeatures di{};
  VkPhysicalDeviceVulkan13Features vulkan13Features{};

  bda.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
  bda.pNext = &di;
  di.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

  vulkan13Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  vulkan13Features.pNext = &bda;

  VkPhysicalDeviceFeatures2 features2{};
  features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features2.pNext = &vulkan13Features;

  vkGetPhysicalDeviceFeatures2(device, &features2);

  return vulkan13Features.dynamicRendering == VK_TRUE &&
      vulkan13Features.shaderDemoteToHelperInvocation == VK_TRUE &&
      bda.bufferDeviceAddress == VK_TRUE &&
      di.descriptorBindingPartiallyBound == VK_TRUE &&
      di.runtimeDescriptorArray == VK_TRUE &&
      di.shaderSampledImageArrayNonUniformIndexing == VK_TRUE &&
      di.descriptorBindingVariableDescriptorCount == VK_TRUE &&
      di.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE;
}

u32 ScoreGPU(VkPhysicalDevice device) {
  if (!HasRequiredQueues(device)) {
    return 0;
  }

  if (!HasRequiredExtensions(device)) {
    return 0;
  }

  if (!HasRequiredFeatures(device)) {
    return 0;
  }

  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(device, &properties);

  u32 score = 1;

  if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
    score += 1000;
  }

  score += properties.limits.maxImageDimension2D;

  return score;
}

void Device::PickPhysicalDevice() {
  VL_PROFILE_ZONE_N("Device::PickPhysicalDevice");
  u32 deviceCount = 0;
  VkResult result =
      vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
  std::cout << "vkEnumeratePhysicalDevices(count) result = " << result
            << ", deviceCount = " << deviceCount << "\n";
  VK_CHECK(result, "Failed to enumerate physical devices");

  if (deviceCount == 0) {
    throw std::runtime_error("No Vulkan physical devices found");
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  result = vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
  std::cout << "vkEnumeratePhysicalDevices(fill) result = " << result
            << ", deviceCount = " << deviceCount << "\n";
  VK_CHECK(result, "Failed to enumerate physical devices");

  u32 maxScore = 0;
  VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

  for (VkPhysicalDevice device : devices) {
    uint32_t deviceScore = ScoreGPU(device);
    if (deviceScore > maxScore) {
      maxScore = deviceScore;
      bestDevice = device;
    }
  }

  if (bestDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("No suitable Vulkan physical device found");
  }

  physicalDevice_ = bestDevice;

  vkGetPhysicalDeviceProperties(physicalDevice_, &physicalDeviceProperties_);

  std::cout << "[Device] Selected GPU: "
            << physicalDeviceProperties_.deviceName << "\n";

  std::cout << "[Device] API version: "
            << VK_VERSION_MAJOR(physicalDeviceProperties_.apiVersion) << "."
            << VK_VERSION_MINOR(physicalDeviceProperties_.apiVersion) << "."
            << VK_VERSION_PATCH(physicalDeviceProperties_.apiVersion) << '\n';
}

void Device::CreateLogicalDevice() {
  VL_PROFILE_ZONE_N("Device::CreateLogicalDevice");
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

  VkPhysicalDeviceBufferDeviceAddressFeatures bda{};
  bda.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
  bda.bufferDeviceAddress = VK_TRUE;

  VkPhysicalDeviceDescriptorIndexingFeatures di{};
  di.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
  di.descriptorBindingPartiallyBound = VK_TRUE;
  di.runtimeDescriptorArray = VK_TRUE;
  di.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  di.descriptorBindingVariableDescriptorCount = VK_TRUE;
  di.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
  bda.pNext = &di;

  VkPhysicalDeviceVulkan13Features vulkan13Features{};
  vulkan13Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  vulkan13Features.dynamicRendering = VK_TRUE;
  vulkan13Features.shaderDemoteToHelperInvocation = VK_TRUE;
  vulkan13Features.pNext = &bda;

  const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.pNext = &vulkan13Features;
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

void Device::CreateCommandObjects() {
  VL_PROFILE_ZONE_N("Device::CreateCommandObjects");

  VkCommandPoolCreateInfo framePoolInfo{};
  framePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  framePoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  framePoolInfo.queueFamilyIndex = graphicsQueueFamily_;

  VK_CHECK(vkCreateCommandPool(device_, &framePoolInfo, nullptr, &commandPool_),
           "Failed to create Vulkan command pool");

  VkCommandPoolCreateInfo uploadPoolInfo{};
  uploadPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  uploadPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                         VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  uploadPoolInfo.queueFamilyIndex = graphicsQueueFamily_;

  VK_CHECK(vkCreateCommandPool(device_, &uploadPoolInfo, nullptr,
                               &uploadCommandPool_),
           "Failed to create upload command pool");

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool_;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = k_MaxFramesInFlight;

  VK_CHECK(
      vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()),
      "Failed to allocate Vulkan command buffer");

#if VL_PROFILING
  tracyContext_ = VL_PROFILE_GPU_CONTEXT(physicalDevice_, device_,
                                         graphicsQueue_, commandBuffers_[0]);
#endif

  for (u32 i = 0; i < k_MaxFramesInFlight; i++) {
    commandLists_[i] =
        std::make_unique<CommandList>(*this, commandBuffers_[i]);
  }
}

void Device::CreateSyncObjects() {
  VL_PROFILE_ZONE_N("Device::CreateSyncObjects");

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (u32 i = 0; i < k_MaxFramesInFlight; ++i) {
    VK_CHECK(vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                               &frames_[i].imageAvailableSemaphore),
             "Failed to create image available semaphore");

    VK_CHECK(
        vkCreateFence(device_, &fenceInfo, nullptr, &frames_[i].inFlightFence),
        "Failed to create in-flight fence");
  }
}

void Device::CreateSwapchainSyncObjects() {
  VL_PROFILE_ZONE_N("Device::CreateSwapchainSyncObjects");

  if (!swapchain_) {
    throw std::runtime_error(
        "Cannot create swapchain sync objects without a swapchain");
  }

  DestroySwapchainSyncObjects();

  const u32 imageCount = swapchain_->GetImageCount();

  swapchainImagesInFlight_.assign(imageCount, VK_NULL_HANDLE);
  swapchainRenderFinishedSemaphores_.resize(imageCount, VK_NULL_HANDLE);

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  for (u32 i = 0; i < imageCount; ++i) {
    VK_CHECK(vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                               &swapchainRenderFinishedSemaphores_[i]),
             "Failed to create swapchain render-finished semaphore");
  }
}

void Device::DestroySwapchainSyncObjects() {
  for (VkSemaphore semaphore : swapchainRenderFinishedSemaphores_) {
    if (semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_, semaphore, nullptr);
    }
  }

  swapchainRenderFinishedSemaphores_.clear();
  swapchainImagesInFlight_.clear();
}

void Device::CollectGarbage() {
  // no-op
}

std::unique_ptr<IUploadContext>
Device::CreateUploadContext(u64 stagingBufferSize) {
  return std::make_unique<UploadContext>(*this, stagingBufferSize);
}

u32 Device::FindMemoryType(u32 typeFilter,
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

SwapchainHandle Device::CreateSwapchain(const SwapchainDesc &desc) {
  if (swapchain_) {
    throw std::runtime_error("Only one swapchain is supported for now");
  }

  swapchain_ = std::make_unique<Swapchain>(
      instance_, physicalDevice_, device_, presentQueueFamily_, desc);

  swapchainImageHandles_.clear();
  swapchainImageViewHandles_.clear();

  for (u32 i = 0; i < swapchain_->GetImageCount(); ++i) {
    const SwapchainImage &swapImage = swapchain_->GetImage(i);

    Image wrappedImage{};
    wrappedImage.image = swapImage.image;
    wrappedImage.memory = VK_NULL_HANDLE;
    wrappedImage.format =
        Format::BGRA8_UNORM; // later: derive from swapchain_->GetFormat()
    wrappedImage.usage = ImageUsage::ColorAttachment;
    wrappedImage.mipLayouts.resize(1, ImageLayout::Undefined);
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

    ImageView vkView{};
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

void Device::ClearCurrentSwapchainImage(float r, float g, float b,
                                              float a) {
  VL_PROFILE_ZONE_N("Device::ClearCurrentSwapchainImage");
  if (!swapchain_) {
    throw std::runtime_error("No swapchain available to clear");
  }

  VkCommandBuffer cmd = commandBuffers_[currentFrame_];

  const SwapchainImage &image =
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

  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
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

  vkCmdClearColorImage(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       &clearColor, 1, &range);

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

  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toPresent);
}

void Device::WaitIdle() {
  if (device_ != VK_NULL_HANDLE) {
    VK_CHECK(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle failed");
  }
}

void Device::DestroySwapchain(SwapchainHandle handle) {
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

void Device::ResizeSwapchain(SwapchainHandle handle, u32 width,
                                   u32 height) {
  VL_PROFILE_ZONE_N("Device::ResizeSwapchain");

  if (!handle.IsValid()) {
    throw std::runtime_error("ResizeSwapchain called with invalid handle");
  }

  if (!swapchain_) {
    throw std::runtime_error(
        "ResizeSwapchain called without an existing swapchain");
  }

  if (width == 0 || height == 0) {
    return;
  }

  vkDeviceWaitIdle(device_);

  DestroySwapchainSyncObjects();

  for (ImageViewHandle view : swapchainImageViewHandles_) {
    DestroyImageView(view);
  }
  swapchainImageViewHandles_.clear();

  for (ImageHandle image : swapchainImageHandles_) {
    DestroyImage(image);
  }
  swapchainImageHandles_.clear();

  SwapchainDesc desc = swapchain_->GetDesc();
  desc.width = width;
  desc.height = height;

  swapchain_.reset();

  swapchain_ = std::make_unique<Swapchain>(
      instance_, physicalDevice_, device_, presentQueueFamily_, desc);

  for (u32 i = 0; i < swapchain_->GetImageCount(); ++i) {
    const SwapchainImage &swapImage = swapchain_->GetImage(i);

    Image wrappedImage{};
    wrappedImage.image = swapImage.image;
    wrappedImage.memory = VK_NULL_HANDLE;
    wrappedImage.format =
        Format::BGRA8_UNORM; // later derive from actual swapchain format
    wrappedImage.usage = ImageUsage::ColorAttachment;
    wrappedImage.mipLayouts.resize(1, ImageLayout::Undefined);
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

    ImageView vkView{};
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
  currentBackbufferIndex_ = 0;
}

BufferHandle Device::CreateBuffer(const BufferDesc &desc) {
  VL_PROFILE_ZONE_N("Device::CreateBuffer");

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

  Buffer buffer{};
  buffer.size = desc.size;
  buffer.usage = desc.usage;
  buffer.memoryUsage = desc.memoryUsage;

  VmaAllocationCreateInfo allocInfo =
      ToVmaAllocationCreateInfo(desc.memoryUsage);

  if (HasFlag(desc.usage, BufferUsage::ShaderDeviceAddress)) {
    allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
  }

  VkResult result =
      vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &buffer.buffer,
                      &buffer.allocation, &buffer.allocationInfo);

  if (result != VK_SUCCESS) {
    throw std::runtime_error("CreateBuffer: vmaCreateBuffer failed");
  }

  if (HasFlag(desc.usage, BufferUsage::ShaderDeviceAddress)) {
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = buffer.buffer;

    buffer.deviceAddress = vkGetBufferDeviceAddress(device_, &addressInfo);
  }

  if (desc.initialData != nullptr) {
    if (desc.memoryUsage == MemoryUsage::GPUOnly) {
      vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
      throw std::runtime_error("CreateBuffer: initialData for GPUOnly buffers "
                               "requires staging upload path");
    }

    if (buffer.allocationInfo.pMappedData == nullptr) {
      void *mappedData = nullptr;
      result = vmaMapMemory(allocator_, buffer.allocation, &mappedData);

      if (result != VK_SUCCESS || mappedData == nullptr) {
        vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
        throw std::runtime_error("CreateBuffer: vmaMapMemory failed");
      }

      memcpy(mappedData, desc.initialData, static_cast<size_t>(desc.size));
      vmaUnmapMemory(allocator_, buffer.allocation);
    } else {
      memcpy(buffer.allocationInfo.pMappedData, desc.initialData,
             static_cast<size_t>(desc.size));
    }

    vmaFlushAllocation(allocator_, buffer.allocation, 0, desc.size);
  }

  const u32 handleId = nextBufferHandle_++;
  buffers_.emplace(handleId, buffer);

  setObjectDebugName(device_, VK_OBJECT_TYPE_BUFFER,
                     reinterpret_cast<uint64_t>(buffer.buffer), desc.debugName);

  return BufferHandle{handleId};
}

void Device::DestroyBuffer(BufferHandle handle) {
  auto it = buffers_.find(handle.id);
  if (it == buffers_.end()) {
    return;
  }

  auto &buffer = it->second;

  if (buffer.buffer != VK_NULL_HANDLE) {
    vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
  }

  buffers_.erase(it);
}

const Buffer &Device::GetBuffer(BufferHandle handle) const {
  auto it = buffers_.find(handle.id);

  if (it == buffers_.end()) {
    throw std::runtime_error("Invalid texture handle");
  }

  return it->second;
}

u64 Device::GetBufferDeviceAddress(BufferHandle handle) const {
  Buffer buffer = GetBuffer(handle);
  return static_cast<u64>(buffer.deviceAddress);
}

ImageHandle Device::CreateImage(const ImageDesc &desc) {
  VL_PROFILE_ZONE_N("Device::CreateImage");

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

  if (desc.mipLevels == 0) {
    throw std::runtime_error("CreateImage: mipLevels must be greater than 0");
  }

  if (desc.arrayLayers == 0) {
    throw std::runtime_error("CreateImage: arrayLayers must be greater than 0");
  }

  if (desc.type == ImageType::Cube) {
    if (desc.width != desc.height) {
      throw std::runtime_error(
          "CreateImage: cube textures must have width == height");
    }

    if (desc.depth != 1) {
      throw std::runtime_error(
          "CreateImage: cube textures must have depth == 1");
    }

    if (desc.arrayLayers != 6) {
      throw std::runtime_error(
          "CreateImage: cube textures must have exactly 6 array layers");
    }
  }

  VkImageCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  createInfo.flags = 0;
  createInfo.imageType = ToVkImageType(desc.type);
  createInfo.format = ToVkFormat(desc.format);
  createInfo.extent = {desc.width, desc.height, desc.depth};
  createInfo.mipLevels = desc.mipLevels;
  createInfo.arrayLayers = desc.arrayLayers;
  createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  createInfo.usage = ToVkImageUsage(desc.usage);
  createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  if (desc.type == ImageType::Cube) {
    createInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
  }

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

  Image vkImage{};
  vkImage.image = image;
  vkImage.memory = memory;
  vkImage.type = desc.type;
  vkImage.format = desc.format;
  vkImage.usage = desc.usage;
  vkImage.mipLayouts.resize(desc.mipLevels, ImageLayout::Undefined);
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
void Device::DestroyImage(ImageHandle handle) {
  if (!handle.IsValid()) {
    return;
  }

  auto it = images_.find(handle.id);
  if (it == images_.end()) {
    return;
  }

  Image &image = it->second;

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

const Image &Device::GetImage(ImageHandle handle) const {

  auto it = images_.find(handle.id);
  if (it == images_.end()) {
    throw std::runtime_error("Invalid image handle");
  }

  return it->second;
}

void Device::SetImageLayout(ImageHandle handle, u32 baseMipLevel,
                                  u32 mipLevelCount, ImageLayout layout) {
  auto it = images_.find(handle.id);
  if (it == images_.end()) {
    throw std::runtime_error("Invalid image handle");
  }

  Image &image = it->second;
  if (baseMipLevel >= image.mipLayouts.size()) {
    throw std::runtime_error("SetImageLayout: mip level out of bounds");
  }

  const u32 endMip = std::min<u32>(baseMipLevel + mipLevelCount,
                                   static_cast<u32>(image.mipLayouts.size()));

  for (u32 mip = baseMipLevel; mip < endMip; ++mip) {
    image.mipLayouts[mip] = layout;
  }
}

ImageViewHandle Device::CreateImageView(const ImageViewDesc &desc) {
  if (!desc.image.IsValid()) {
    throw std::runtime_error("CreateImageView: invalid image handle");
  }

  const Image &vkImage = GetImage(desc.image);

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

  if (desc.type == ImageViewType::Cube) {
    if (desc.arrayLayerCount != 6) {
      throw std::runtime_error(
          "CreateImageView: cube view must have arrayLayerCount == 6");
    }

    if (vkImage.type != ImageType::Cube) {
      throw std::runtime_error(
          "CreateImageView: source image is not a cube image");
    }
  }

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = vkImage.image;
  viewInfo.viewType = ToVkImageViewType(desc.type);
  viewInfo.format = ToVkFormat(viewFormat);
  viewInfo.subresourceRange.aspectMask = ToVkImageAspect(aspect);
  viewInfo.subresourceRange.baseMipLevel = desc.baseMipLevel;
  viewInfo.subresourceRange.levelCount = desc.mipLevelCount;
  viewInfo.subresourceRange.baseArrayLayer = desc.baseArrayLayer;
  viewInfo.subresourceRange.layerCount = desc.arrayLayerCount;
  viewInfo.pNext = nullptr;
  viewInfo.flags = 0;

  VkImageView view = VK_NULL_HANDLE;
  VkResult result = vkCreateImageView(device_, &viewInfo, nullptr, &view);
  if (result != VK_SUCCESS) {
    throw std::runtime_error("CreateImageView: vkCreateImageView failed");
  }

  ImageView vkView{};
  vkView.view = view;
  vkView.image = desc.image;
  vkView.format = viewFormat;
  vkView.aspect = aspect;
  vkView.owned = true;

  u32 handleId = nextImageViewHandle_++;

  imageViews_.emplace(handleId, vkView);

  return ImageViewHandle{handleId};
}

void Device::DestroyImageView(ImageViewHandle handle) {
  if (!handle.IsValid()) {
    return;
  }

  auto it = imageViews_.find(handle.id);

  if (it == imageViews_.end()) {
    return;
  }

  ImageView &view = it->second;

  if (view.view != VK_NULL_HANDLE && view.owned) {
    vkDestroyImageView(device_, view.view, nullptr);
  }
  view.view = VK_NULL_HANDLE;

  imageViews_.erase(it);
}

const ImageView &
Device::GetImageView(ImageViewHandle handle) const {
  auto it = imageViews_.find(handle.id);
  if (it == imageViews_.end()) {
    throw std::runtime_error("Invalid image view handle");
  }

  return it->second;
}

SamplerHandle Device::CreateSampler(const SamplerDesc &desc) {
  VkSamplerCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  createInfo.magFilter = ToVkFilter(desc.magFilter);
  createInfo.minFilter = ToVkFilter(desc.minFilter);
  createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  createInfo.addressModeU = ToVkSamplerAddressMode(desc.addressU);
  createInfo.addressModeV = ToVkSamplerAddressMode(desc.addressV);
  createInfo.addressModeW = ToVkSamplerAddressMode(desc.addressW);
  createInfo.mipLodBias = 0.0f;
  createInfo.anisotropyEnable = desc.enableAnisotropy ? VK_TRUE : VK_FALSE;
  createInfo.maxAnisotropy = desc.enableAnisotropy ? desc.maxAnisotropy : 1.0f;
  createInfo.compareEnable = VK_FALSE;
  createInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  createInfo.minLod = desc.minLod;
  createInfo.maxLod = desc.maxLod;
  createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  createInfo.unnormalizedCoordinates = VK_FALSE;

  VkSampler sampler = VK_NULL_HANDLE;
  VK_CHECK(vkCreateSampler(device_, &createInfo, nullptr, &sampler),
           "vkCreateSampler: failed to create VkSampler");

  const u32 handleId = nextSamplerHandle_++;
  samplers_.emplace(handleId, Sampler{.sampler = sampler});

  return SamplerHandle{handleId};
}

void Device::DestroySampler(SamplerHandle handle) {
  if (!handle.IsValid()) {
    return;
  }

  auto it = samplers_.find(handle.id);
  if (it == samplers_.end()) {
    return;
  }

  if (it->second.sampler != VK_NULL_HANDLE) {
    vkDestroySampler(device_, it->second.sampler, nullptr);
    it->second.sampler = VK_NULL_HANDLE;
  }

  samplers_.erase(it);
}

const Sampler &Device::GetSampler(SamplerHandle handle) const {
  auto it = samplers_.find(handle.id);
  if (it == samplers_.end()) {
    throw std::runtime_error("Invalid sampler handle");
  }

  return it->second;
}

ShaderHandle Device::CreateShader(const ShaderDesc &desc) {
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
  shaders_.emplace(handleId, Shader{.module = shaderModule,
                                          .stage = desc.stage,
                                          .reflection = desc.reflection});

  return ShaderHandle{handleId};
}

void Device::DestroyShader(ShaderHandle handle) {
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

const Shader &Device::GetShader(ShaderHandle handle) const {
  auto it = shaders_.find(handle.id);
  if (it == shaders_.end()) {
    throw std::runtime_error("Invalid shader handle");
  }

  return it->second;
}

PipelineHandle
Device::CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) {
  VL_PROFILE_ZONE_N("Device::CreateGraphicsPipeline");

  if (!desc.vertexShader.IsValid()) {
    throw std::runtime_error(
        "CreateGraphicsPipeline requires a valid vertex shader");
  }

  if (!desc.fragmentShader.IsValid()) {
    throw std::runtime_error(
        "CreateGraphicsPipeline requires a valid fragment shader");
  }

  const Shader &vs = GetShader(desc.vertexShader);
  const Shader &fs = GetShader(desc.fragmentShader);

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

  if (desc.blend.enable) {
    colorBlendAttachment.srcColorBlendFactor =
        ToVkBlendFactor(desc.blend.srcColor);
    colorBlendAttachment.dstColorBlendFactor =
        ToVkBlendFactor(desc.blend.dstColor);
    colorBlendAttachment.colorBlendOp = ToVkBlendOp(desc.blend.colorOp);

    colorBlendAttachment.srcAlphaBlendFactor =
        ToVkBlendFactor(desc.blend.srcAlpha);
    colorBlendAttachment.dstAlphaBlendFactor =
        ToVkBlendFactor(desc.blend.dstAlpha);
    colorBlendAttachment.alphaBlendOp = ToVkBlendOp(desc.blend.alphaOp);
  } else {
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;

    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  }
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

  std::vector<VkPushConstantRange> pushConstantRanges = {};
  pushConstantRanges.reserve(vs.reflection.pushConstants.size() +
                             fs.reflection.pushConstants.size());

  auto addOrMergePushConstantRange = [&](uint32_t offset, uint32_t size,
                                         VkShaderStageFlags stageFlags) {
    const uint32_t newStart = offset;
    const uint32_t newEnd = offset + size;

    for (auto &existing : pushConstantRanges) {
      const uint32_t existingStart = existing.offset;
      const uint32_t existingEnd = existing.offset + existing.size;

      const bool overlaps =
          !(newEnd <= existingStart || newStart >= existingEnd);

      if (overlaps) {
        const uint32_t mergedStart = std::min(existingStart, newStart);
        const uint32_t mergedEnd = std::max(existingEnd, newEnd);

        existing.offset = mergedStart;
        existing.size = mergedEnd - mergedStart;
        existing.stageFlags |= stageFlags;
        return;
      }

      // Optional: also merge directly adjacent ranges
      const bool adjacent =
          (newEnd == existingStart || newStart == existingEnd);
      if (adjacent) {
        const uint32_t mergedStart = std::min(existingStart, newStart);
        const uint32_t mergedEnd = std::max(existingEnd, newEnd);

        existing.offset = mergedStart;
        existing.size = mergedEnd - mergedStart;
        existing.stageFlags |= stageFlags;
        return;
      }
    }

    VkPushConstantRange pcr{};
    pcr.offset = offset;
    pcr.size = size;
    pcr.stageFlags = stageFlags;
    pushConstantRanges.push_back(pcr);
  };

  for (const auto &pushConstantRange : vs.reflection.pushConstants) {
    addOrMergePushConstantRange(pushConstantRange.offset,
                                pushConstantRange.size,
                                VK_SHADER_STAGE_VERTEX_BIT);
  }

  for (const auto &pushConstantRange : fs.reflection.pushConstants) {
    addOrMergePushConstantRange(pushConstantRange.offset,
                                pushConstantRange.size,
                                VK_SHADER_STAGE_FRAGMENT_BIT);
  }

  std::vector<VkDescriptorSetLayout> vkSetLayouts;
  vkSetLayouts.reserve(desc.layout.descriptorSetLayoutCount);

  for (u32 i = 0; i < desc.layout.descriptorSetLayoutCount; ++i) {
    BindingLayoutHandle handle = desc.layout.descriptorSetLayouts[i];
    const BindingLayout &vkLayout =
        descriptorSetLayouts_[handle.id];
    vkSetLayouts.push_back(vkLayout.layout);
  }

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<u32>(vkSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts =
      vkSetLayouts.empty() ? nullptr : vkSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();
  pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();

  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VK_CHECK(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr,
                                  &pipelineLayout),
           "Failed to create Vulkan pipeline layout");

  const bool hasColor = desc.colorFormat != Format::Undefined;

  const bool hasDepth = desc.depth.depthFormat != Format::Undefined;

  if (!hasColor && !hasDepth) {
    vkDestroyPipelineLayout(device_, pipelineLayout, nullptr);

    throw std::runtime_error(
        "Graphics pipeline requires at least one attachment");
  }

  VkFormat colorFormat =
      hasColor ? ToVkFormat(desc.colorFormat) : VK_FORMAT_UNDEFINED;

  VkFormat depthFormat =
      hasDepth ? ToVkFormat(desc.depth.depthFormat) : VK_FORMAT_UNDEFINED;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable =
      desc.depth.depthTestEnable ? VK_TRUE : VK_FALSE;
  depthStencil.depthWriteEnable =
      desc.depth.depthWriteEnable ? VK_TRUE : VK_FALSE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  VkPipelineRenderingCreateInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  renderingInfo.colorAttachmentCount = hasColor ? 1 : 0;
  renderingInfo.pColorAttachmentFormats = hasColor ? &colorFormat : nullptr;
  renderingInfo.depthAttachmentFormat = depthFormat;
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
      handleId, Pipeline{.pipeline = pipeline, .layout = pipelineLayout});

  return PipelineHandle{handleId};
}

void Device::DestroyPipeline(PipelineHandle handle) {
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

PipelineHandle
Device::CreateComputePipeline(const ComputePipelineDesc &desc) {
  VL_PROFILE_ZONE_N("Device::CreateComputePipeline");

  if (!desc.computeShader.IsValid()) {
    throw std::runtime_error(
        "CreateComputePipeline requires a valid compute shader");
  }

  const Shader &cs = GetShader(desc.computeShader);

  VkPipelineShaderStageCreateInfo shaderStage{};
  shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  shaderStage.module = cs.module;
  shaderStage.pName = "main";

  std::vector<VkPushConstantRange> pushConstantRanges;
  pushConstantRanges.reserve(cs.reflection.pushConstants.size());

  for (const auto &range : cs.reflection.pushConstants) {
    VkPushConstantRange vkRange{};
    vkRange.offset = range.offset;
    vkRange.size = range.size;
    vkRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRanges.push_back(vkRange);
  }

  std::vector<VkDescriptorSetLayout> vkSetLayouts;
  vkSetLayouts.reserve(desc.layout.descriptorSetLayoutCount);

  for (u32 i = 0; i < desc.layout.descriptorSetLayoutCount; ++i) {
    BindingLayoutHandle handle = desc.layout.descriptorSetLayouts[i];

    const BindingLayout &vkLayout =
        descriptorSetLayouts_.at(handle.id);

    vkSetLayouts.push_back(vkLayout.layout);
  }

  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.setLayoutCount = static_cast<u32>(vkSetLayouts.size());
  layoutInfo.pSetLayouts = vkSetLayouts.empty() ? nullptr : vkSetLayouts.data();
  layoutInfo.pushConstantRangeCount =
      static_cast<u32>(pushConstantRanges.size());
  layoutInfo.pPushConstantRanges =
      pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();

  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VK_CHECK(
      vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout),
      "Failed to create Vulkan compute pipeline layout");

  VkComputePipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.stage = shaderStage;
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineInfo.basePipelineIndex = -1;

  VkPipeline pipeline = VK_NULL_HANDLE;
  VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1,
                                             &pipelineInfo, nullptr, &pipeline);

  if (result != VK_SUCCESS) {
    vkDestroyPipelineLayout(device_, pipelineLayout, nullptr);
    throw std::runtime_error("Failed to create Vulkan compute pipeline");
  }

  const u32 handleId = nextPipelineHandle_++;
  pipelines_.emplace(
      handleId, Pipeline{.pipeline = pipeline, .layout = pipelineLayout});

  return PipelineHandle{handleId};
}

const Pipeline &Device::GetPipeline(PipelineHandle handle) const {
  auto it = pipelines_.find(handle.id);
  if (it == pipelines_.end()) {
    throw std::runtime_error("Invalid shader handle");
  }

  return it->second;
}

BindingLayoutHandle
Device::CreateBindingLayout(const BindingLayoutDesc &desc) {
  std::vector<VkDescriptorSetLayoutBinding> vkBindings;
  std::vector<VkDescriptorBindingFlags> vkBindingFlags;
  vkBindings.reserve(desc.bindingCount);
  vkBindingFlags.reserve(desc.bindingCount);

  bool usesUpdateAfterBind = false;
  bool usesBindingFlags = false;

  for (u32 i = 0; i < desc.bindingCount; ++i) {
    const BindingDesc &binding = desc.bindings[i];
    VkDescriptorSetLayoutBinding vkBinding{};
    vkBinding.binding = binding.binding;
    vkBinding.descriptorType = ToVkDescriptorType(binding.type);
    vkBinding.descriptorCount = binding.count;
    vkBinding.stageFlags = ToVkShaderStageFlags(binding.visibility);
    vkBinding.pImmutableSamplers = nullptr;

    vkBindings.push_back(vkBinding);

    VkDescriptorBindingFlags flags = 0;

    if (HasFlag(binding.flags, BindingFlags::PartiallyBound)) {
        flags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    }

    if (HasFlag(binding.flags, BindingFlags::VariableCount)) {
        flags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    }

    if (HasFlag(binding.flags, BindingFlags::UpdateAfterBind)) {
        flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        usesUpdateAfterBind = true;
    }

    usesBindingFlags |= flags != 0;
    vkBindingFlags.push_back(flags);
  }

  VkDescriptorSetLayoutBindingFlagsCreateInfo fci{};
  fci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
  fci.bindingCount = static_cast<u32>(vkBindingFlags.size());
  fci.pBindingFlags = vkBindingFlags.data();

  VkDescriptorSetLayoutCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  createInfo.bindingCount = static_cast<u32>(vkBindings.size());
  createInfo.pBindings = vkBindings.data();

  if (usesBindingFlags) {
      createInfo.pNext = &fci;
  }

  if (usesUpdateAfterBind) {
      createInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
  }

  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  VK_CHECK(
      vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &layout),
      "vkCreateDescriptorSetLayout: failed to create Descriptor Set Layout");

  const u32 handleId = nextBindingLayoutHandle_++;

  descriptorSetLayouts_.emplace(handleId,
                                BindingLayout{.layout = layout, .usesUpdateAfterBind = usesUpdateAfterBind});

  return BindingLayoutHandle{handleId};
}

void Device::DestroyBindingLayout(
    BindingLayoutHandle handle) {
  if (!handle.IsValid()) {
    return;
  }

  auto it = descriptorSetLayouts_.find(handle.id);
  if (it == descriptorSetLayouts_.end()) {
    return;
  }

  if (it->second.layout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device_, it->second.layout, nullptr);
    it->second.layout = VK_NULL_HANDLE;
  }

  descriptorSetLayouts_.erase(it);
}

const BindingLayout &
Device::GetBindingLayout(BindingLayoutHandle handle) const {
  auto it = descriptorSetLayouts_.find(handle.id);
  if (it == descriptorSetLayouts_.end()) {
    throw std::runtime_error("Invalid shader handle");
  }

  return it->second;
}

BindingPoolHandle
Device::CreateBindingPool(const BindingPoolDesc &desc) {
  std::vector<VkDescriptorPoolSize> vkPoolSizes;
  vkPoolSizes.reserve(desc.poolSizeCount);

  for (u32 i = 0; i < desc.poolSizeCount; ++i) {
    const BindingPoolSize &size = desc.poolSizes[i];

    VkDescriptorPoolSize vkSize{};
    vkSize.type = ToVkDescriptorType(size.type);
    vkSize.descriptorCount = size.count;
    vkPoolSizes.push_back(vkSize);
  }

  VkDescriptorPoolCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  createInfo.poolSizeCount = static_cast<u32>(vkPoolSizes.size());
  createInfo.pPoolSizes = vkPoolSizes.data();
  createInfo.maxSets = desc.maxSets;
  if (HasFlag(desc.flags, BindingPoolFlags::UpdateAfterBind)) {
      createInfo.flags |=
          VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  }

  VkDescriptorPool pool = VK_NULL_HANDLE;
  VK_CHECK(vkCreateDescriptorPool(device_, &createInfo, nullptr, &pool),
           "vkCreateDescriptorPool: failed to create Descriptor Pool");
  const u32 handleId = nextBindingPoolHandle_++;

  descriptorPools_.emplace(handleId, BindingPool{.pool = pool, .supportsUpdateAfterBind = HasFlag(desc.flags, BindingPoolFlags::UpdateAfterBind)});

  return BindingPoolHandle{handleId};
}

void Device::DestroyBindingPool(BindingPoolHandle handle) {
  if (!handle.IsValid()) {
    return;
  }

  auto it = descriptorPools_.find(handle.id);
  if (it == descriptorPools_.end()) {
    return;
  }

  for (auto setIt = descriptorSets_.begin(); setIt != descriptorSets_.end();) {
    if (setIt->second.pool.id == handle.id) {
      setIt = descriptorSets_.erase(setIt);
    } else {
      ++setIt;
    }
  }

  if (it->second.pool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device_, it->second.pool, nullptr);
    it->second.pool = VK_NULL_HANDLE;
  }

  descriptorPools_.erase(it);
}

const BindingPool &
Device::GetBindingPool(BindingPoolHandle handle) const {
  auto it = descriptorPools_.find(handle.id);
  if (it == descriptorPools_.end()) {
    throw std::runtime_error("Invalid shader handle");
  }

  return it->second;
}

BindingSetHandle
Device::AllocateBindingSet(const BindingSetAllocationDesc& desc) {
  const BindingPool &vkPool = GetBindingPool(desc.pool);
  const BindingLayout &vkLayout = GetBindingLayout(desc.layout);

  if (vkLayout.usesUpdateAfterBind && !vkPool.supportsUpdateAfterBind) {
      throw std::runtime_error("Update-after-bind layout requires compatible binding pool");
  }

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = vkPool.pool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &vkLayout.layout;

  VkDescriptorSetVariableDescriptorCountAllocateInfo countInfo{};
  if (desc.variableBindingCount > 0) {
      countInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
      countInfo.descriptorSetCount = 1;
      countInfo.pDescriptorCounts = &desc.variableBindingCount;

      allocInfo.pNext = &countInfo;
  }


  VkDescriptorSet set = VK_NULL_HANDLE;
  VK_CHECK(vkAllocateDescriptorSets(device_, &allocInfo, &set),
           "vkAllocateDescriptorSets: failed to allocate Descriptor Sets");

  u32 handleId = nextBindingSetHandle_++;
  descriptorSets_.emplace(handleId, BindingSet{
                                        .set = set,
                                        .layout = desc.layout,
                                        .pool = desc.pool,
                                    });

  return BindingSetHandle{handleId};
}

void Device::UpdateBindingSet(const BindingWriteDesc &desc) {
  if (!desc.dstSet) {
    throw std::runtime_error(
        "UpdateBindingSet: invalid destination descriptor set");
  }

  if (desc.descriptorCount == 0) {
    throw std::runtime_error(
        "UpdateBindingSet: descriptorCount must be greater than zero");
  }

  const BindingSet& vkSet = GetBindingSet(desc.dstSet);

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = vkSet.set;
  write.dstBinding = desc.binding;
  write.dstArrayElement = desc.arrayElement;
  write.descriptorCount = desc.descriptorCount;
  write.descriptorType = ToVkDescriptorType(desc.type);

  std::vector<VkDescriptorBufferInfo> vkBufferInfos{};
  std::vector<VkDescriptorImageInfo> vkImageInfos{};
  vkBufferInfos.resize(desc.descriptorCount);
  vkImageInfos.resize(desc.descriptorCount);

  switch (desc.type) {
  case BindingType::UniformBuffer: {
    if (desc.bufferInfo == nullptr) {
      throw std::runtime_error(
          "UpdateBindingSet: bufferInfo is null for UniformBuffer");
    }

    for (u32 i = 0; i < desc.descriptorCount; i++) {
        const BindingBufferInfo& bufferInfo = desc.bufferInfo[i];
        vkBufferInfos[i] = {
            .buffer = GetBuffer(bufferInfo.buffer).buffer,
            .offset = bufferInfo.offset,
            .range = bufferInfo.range,
        };
    }

    write.pBufferInfo = vkBufferInfos.data();
    break;
  }

  case BindingType::CombinedImageSampler: {
    if (desc.imageInfo == nullptr) {
      throw std::runtime_error(
          "UpdateBindingSet: imageInfo is null for CombinedImageSampler");
    }

    for (u32 i = 0; i < desc.descriptorCount; i++) {
        const BindingImageInfo& imageInfo = desc.imageInfo[i];
        vkImageInfos[i] = {
            .sampler = GetSampler(imageInfo.sampler).sampler,
            .imageView = GetImageView(imageInfo.imageView).view,
            .imageLayout = ToVkImageLayout(imageInfo.imageLayout),
        };
    }

    write.pImageInfo = vkImageInfos.data();
    break;
  }

  case BindingType::StorageImage: {
    if (desc.imageInfo == nullptr) {
      throw std::runtime_error(
          "UpdateBindingSet: imageInfo is null for StorageImage");
    }


    for (u32 i = 0; i < desc.descriptorCount; i++) {
        const BindingImageInfo& imageInfo = desc.imageInfo[i];
        const ImageView& vkImageView = GetImageView(imageInfo.imageView);
        vkImageInfos[i] = {
            .sampler = VK_NULL_HANDLE,
            .imageView = vkImageView.view,
            .imageLayout = ToVkImageLayout(imageInfo.imageLayout),
        };
    }

    write.pImageInfo = vkImageInfos.data();
    break;
  }

  case BindingType::StorageBuffer: {
    if (desc.bufferInfo == nullptr) {
      throw std::runtime_error(
          "UpdateBindingSet: bufferInfo is null for StorageBuffer");
    }


    for (u32 i = 0; i < desc.descriptorCount; i++) {
        const BindingBufferInfo& bufferInfo = desc.bufferInfo[i];
        vkBufferInfos[i] = {
            .buffer = GetBuffer(bufferInfo.buffer).buffer,
            .offset = bufferInfo.offset,
            .range = bufferInfo.range,
        };
    }

    write.pBufferInfo = vkBufferInfos.data();

    break;
  }

  default:
    throw std::runtime_error(
        "UpdateBindingSet: unsupported descriptor type");
  }

  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

const BindingSet &
Device::GetBindingSet(BindingSetHandle handle) const {
  auto it = descriptorSets_.find(handle.id);
  if (it == descriptorSets_.end()) {
    throw std::runtime_error("Invalid binding set handle");
  }

  return it->second;
}

ImageLayout Device::GetImageLayout(ImageHandle imageHandle,
                                         u32 mipLevel) const {
  if (!imageHandle.IsValid()) {
    throw std::runtime_error("GetImageLayout: invalid image handle");
  }

  const Image &image = GetImage(imageHandle);

  if (mipLevel >= image.mipLayouts.size()) {
    throw std::runtime_error("GetImageLayout: mip level out of bounds");
  }

  return image.mipLayouts[mipLevel];
}

FrameBeginResult Device::BeginFrame(SwapchainHandle swapchain) {
  VL_PROFILE_ZONE_N("Device::BeginFrame");

  if (!swapchain.IsValid()) {
    throw std::runtime_error("BeginFrame called with invalid swapchain handle");
  }

  if (!swapchain_) {
    throw std::runtime_error("BeginFrame called without a created swapchain");
  }

  const u32 frameIndex = currentFrame_;
  FrameSyncData &frame = frames_[frameIndex];

  {
    VL_PROFILE_ZONE_N("BeginFrame.WaitForFrameFence");
    VK_CHECK(
        vkWaitForFences(device_, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX),
        "Failed to wait for frame fence");
  }

  u32 imageIndex = 0;
  VkResult result;
  {
    VL_PROFILE_ZONE_N("BeginFrame.AcquireNextImage");
    result = vkAcquireNextImageKHR(device_, swapchain_->GetVkSwapchain(),
                                   UINT64_MAX, frame.imageAvailableSemaphore,
                                   VK_NULL_HANDLE, &imageIndex);
  }

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return FrameBeginResult{
        .commandList = CommandListHandle{},
        .backbuffer = ImageViewHandle{},
        .backbufferImage = ImageHandle{},
        .backbufferIndex = 0,
        .success = false,
        .frameIndex = frameIndex,
    };
  }

  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    VK_CHECK(result, "Failed to acquire next swapchain image");
  }

  if (swapchainImagesInFlight_[imageIndex] != VK_NULL_HANDLE) {
    VL_PROFILE_ZONE_N("BeginFrame.WaitForSwapchainImageFence");
    VK_CHECK(vkWaitForFences(device_, 1, &swapchainImagesInFlight_[imageIndex],
                             VK_TRUE, UINT64_MAX),
             "Failed to wait for swapchain image fence");
  }

  swapchainImagesInFlight_[imageIndex] = frame.inFlightFence;

  VK_CHECK(vkResetFences(device_, 1, &frame.inFlightFence),
           "Failed to reset frame fence");

  currentBackbufferIndex_ = imageIndex;

  auto &img = images_.at(swapchainImageHandles_[imageIndex].id);
  img.mipLayouts[0] = ImageLayout::Undefined;

  return FrameBeginResult{
      .commandList = {},
      .backbuffer = swapchainImageViewHandles_[imageIndex],
      .backbufferImage = swapchainImageHandles_[imageIndex],
      .backbufferIndex = imageIndex,
      .success = true,
      .frameIndex = frameIndex,
  };
}

ICommandList &Device::GetCommandList() {

  if (!commandLists_[currentFrame_]) {
    throw std::runtime_error("Command list has not been created");
  }

  return *commandLists_[currentFrame_];
}

void Device::Submit() {
  VL_PROFILE_ZONE_N("Device::Submit");

  FrameSyncData &frame = frames_[currentFrame_];
  VkCommandBuffer cmd = commandBuffers_[currentFrame_];

  VK_CHECK(
      vkWaitForFences(device_, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX),
      "Failed to wait for frame fence");
  VK_CHECK(vkResetFences(device_, 1, &frame.inFlightFence),
           "Failed to reset frame fence");

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  VK_CHECK(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, frame.inFlightFence),
           "Failed to submit Vulkan command buffer");
}

void Device::SubmitAndPresent(SwapchainHandle swapchain) {
  VL_PROFILE_ZONE_N("Device::SubmitAndPresent");

  if (!swapchain.IsValid()) {
    throw std::runtime_error(
        "SubmitAndPresent called with invalid swapchain handle");
  }

  if (!swapchain_) {
    throw std::runtime_error(
        "SubmitAndPresent called without a created swapchain");
  }

  FrameSyncData &frame = frames_[currentFrame_];
  VkCommandBuffer cmd = commandBuffers_[currentFrame_];

  VkSemaphore renderFinishedSemaphore =
      swapchainRenderFinishedSemaphores_[currentBackbufferIndex_];

  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &frame.imageAvailableSemaphore;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

  VK_CHECK(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, frame.inFlightFence),
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

#if VL_PROFILING
  VL_PROFILE_GPU_COLLECT(tracyContext_, cmd);
#endif

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    return;
  }

  VK_CHECK(result, "Failed to present Vulkan swapchain image");

  currentFrame_ = (currentFrame_ + 1) % k_MaxFramesInFlight;
}

void Device::Submit(CommandListHandle handle, VkFence fence) {

  VkCommandBuffer cmd = GetCommandBuffer();

  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;

  VK_CHECK(vkQueueSubmit(graphicsQueue_, 1, &submit, fence),
           "Failed to submit command buffer");
}

Extent2D Device::GetSwapchainDimensions() const {
  return {.width = swapchain_->GetWidth(), .height = swapchain_->GetHeight()};
}
} // namespace Velos::Vulkan
