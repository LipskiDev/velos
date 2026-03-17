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

    for (TextureHandle handle : swapchainTextureHandles_) {
      textures_.erase(handle.id);
    }
    swapchainTextureHandles_.clear();

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

SwapchainHandle VulkanDevice::CreateSwapchain(const SwapchainDesc &desc) {
  if (swapchain_) {
    throw std::runtime_error("Only one swapchain is supported for now");
  }

  swapchain_ = std::make_unique<VulkanSwapchain>(
      instance_, physicalDevice_, device_, presentQueueFamily_, desc);

  swapchainTextureHandles_.clear();
  for (u32 i = 0; i < swapchain_->GetImageCount(); ++i) {
    const VulkanSwapchainImage &image = swapchain_->GetImage(i);

    const u32 handleId = nextTextureHandle_++;
    textures_.emplace(
        handleId,
        VulkanTexture{
            .image = image.image,
            .view = image.view,
            .format =
                Format::BGRA8_UNORM, // or derive properly from swapchain format
            .owned = false});

    swapchainTextureHandles_.push_back(TextureHandle{handleId});
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
  swapchain_.reset();
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

const VulkanTexture &VulkanDevice::GetTexture(TextureHandle handle) const {
  auto it = textures_.find(handle.id);
  if (it == textures_.end()) {
    throw std::runtime_error("Invalid texture handle");
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
  shaders_.emplace(handleId,
                   VulkanShader{.module = shaderModule, .stage = desc.stage});

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

  // No vertex buffers for the first triangle
  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 0;
  vertexInputInfo.pVertexBindingDescriptions = nullptr;
  vertexInputInfo.vertexAttributeDescriptionCount = 0;
  vertexInputInfo.pVertexAttributeDescriptions = nullptr;

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
  vertexPushConstantRanges.reserve(desc.layout.pushConstants.size());

  for (auto &pushConstantRange : desc.layout.pushConstants) {
    VkPushConstantRange pcr;
    pcr.size = pushConstantRange.size;
    pcr.offset = pushConstantRange.offset;
    pcr.stageFlags = ToVkShaderStage(pushConstantRange.stages);
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

  VkPipelineRenderingCreateInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachmentFormats = &colorFormat;
  renderingInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
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
  pipelineInfo.pDepthStencilState = nullptr;
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
        .backbuffer = TextureHandle{},
        .backbufferIndex = 0,
        .success = false,
    });
  }

  if (result == VK_SUBOPTIMAL_KHR) {
    currentBackbufferIndex_ = imageIndex;

    return FrameBeginResult{.commandList = CommandListHandle{1},
                            .backbuffer = swapchainTextureHandles_[imageIndex],
                            .backbufferIndex = imageIndex,
                            .success = true};
  }

  VK_CHECK(result, "Failed to acquire next swapchain image");

  currentBackbufferIndex_ = imageIndex;

  return FrameBeginResult{.commandList = CommandListHandle{1},
                          .backbuffer = swapchainTextureHandles_[imageIndex],
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
