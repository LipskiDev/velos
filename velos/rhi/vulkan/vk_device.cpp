#include "vk_device.h"
#include "../rhi_device.h"
#include "rhi/rhi_handles.h"
#include "rhi/vulkan/vk_common.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace Velos::RHI {

VulkanDevice::VulkanDevice(const DeviceDesc &desc) {
  VK_CHECK(volkInitialize(), "Failed to initialize Volk");

  CreateInstance(desc);
  volkLoadInstance(instance_);

  PickPhysicalDevice();
  CreateLogicalDevice();
  volkLoadDevice(device_);

  CreateCommandObjects();
}

VulkanDevice::~VulkanDevice() {
  commandList_.reset();

  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
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

  std::vector<const char *> extensions = {};

  std::vector<const char *> layers = {};
  if (desc.enableValidation) {
    layers.push_back("VK_LAYER_KHRONOS_validation");
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

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount = 1;
  createInfo.pQueueCreateInfos = &queueCreateInfo;
  createInfo.pEnabledFeatures = &deviceFeatures;

  VK_CHECK(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
           "Failed to create Vulkan logical device");

  vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
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

  commandList_ = std::make_unique<VulkanCommandList>(commandBuffer_);
}

void VulkanDevice::WaitIdle() {
  if (device_ != VK_NULL_HANDLE) {
    VK_CHECK(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle failed");
  }
}

void VulkanDevice::CollectGarbage() {
  // no-op
}

SwapchainHandle VulkanDevice::CreateSwapchain(const SwapchainDesc &) {
  throw std::runtime_error("CreateSwapchain not implemented yet");
}

void VulkanDevice::DestroySwapchain(SwapchainHandle) {
  throw std::runtime_error("DestroySwapchain not implemented yet");
}

void VulkanDevice::ResizeSwapchain(SwapchainHandle, u32, u32) {
  throw std::runtime_error("ResizeSwapchain not implemented yet");
}

BufferHandle VulkanDevice::CreateBuffer(const BufferDesc &) {
  throw std::runtime_error("CreateBuffer not implemented yet");
}

void VulkanDevice::DestroyBuffer(BufferHandle) {
  throw std::runtime_error("DestroyBuffer not implemented yet");
}

TextureHandle VulkanDevice::CreateTexture(const TextureDesc &) {
  throw std::runtime_error("CreateTexture not implemented yet");
}

void VulkanDevice::DestroyTexture(TextureHandle) {
  throw std::runtime_error("DestroyTexture not implemented yet");
}

SamplerHandle VulkanDevice::CreateSampler(const SamplerDesc &) {
  throw std::runtime_error("CreateSampler not implemented yet");
}

void VulkanDevice::DestroySampler(SamplerHandle) {
  throw std::runtime_error("DestroySampler not implemented yet");
}

ShaderHandle VulkanDevice::CreateShader(const ShaderDesc &) {
  throw std::runtime_error("CreateShader not implemented yet");
}

void VulkanDevice::DestroyShader(ShaderHandle) {
  throw std::runtime_error("DestroyShader not implemented yet");
}

PipelineHandle
VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc &) {
  throw std::runtime_error("CreateGraphicsPipeline not implemented yet");
}

void VulkanDevice::DestroyPipeline(PipelineHandle) {
  throw std::runtime_error("DestroyPipeline not implemented yet");
}

FrameBeginResult VulkanDevice::BeginFrame(SwapchainHandle) {
  return FrameBeginResult{
      .commandList = CommandListHandle{1},
      .backbuffer = TextureHandle{},
      .backbufferIndex = 0,
      .success = true,
  };
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

void VulkanDevice::SubmitAndPresent(CommandListHandle, SwapchainHandle) {
  VkCommandBuffer cmd = commandBuffer_;

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  VK_CHECK(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE),
           "Failed to submit command buffer");

  VK_CHECK(vkQueueWaitIdle(graphicsQueue_), "Failed to wait for queue idle");
}

} // namespace Velos::RHI
