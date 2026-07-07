#include "vk_upload_context.h"
#include "rhi/vulkan/vk_command_list.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace Velos::RHI {

static u64 AlignUp(u64 value, u64 alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

VulkanUploadContext::VulkanUploadContext(VulkanDevice &device, u64 size)
    : device_(device), capacity_(size) {

  BufferDesc desc{};
  desc.size = size;
  desc.usage = BufferUsage::TransferSrc;
  desc.memoryUsage = MemoryUsage::CPUToGPU;
  desc.debugName = "Upload Staging Buffer";

  stagingBuffer_ = device_.CreateBuffer(desc);

  auto &buffer = device_.GetBuffer(stagingBuffer_);
  if (!buffer.allocationInfo.pMappedData) {
    throw std::runtime_error("Staging buffer is not mapped");
  }

  mappedPtr_ = static_cast<u8 *>(buffer.allocationInfo.pMappedData);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = device_.GetUploadCommandPool();
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VK_CHECK(vkAllocateCommandBuffers(device_.GetVkDevice(), &allocInfo,
                                    &commandBuffer_),
           "Failed to allocate upload command buffer");

  cmd_ = std::make_unique<VulkanCommandList>(device_, commandBuffer_);

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

  VK_CHECK(vkCreateFence(device_.GetVkDevice(), &fenceInfo, nullptr, &fence_),
           "Failed to create upload fence");
}

VulkanUploadContext::~VulkanUploadContext() {
  if (fence_ != VK_NULL_HANDLE) {
    vkDestroyFence(device_.GetVkDevice(), fence_, nullptr);
  }

  if (commandBuffer_ != VK_NULL_HANDLE) {
    vkFreeCommandBuffers(device_.GetVkDevice(), device_.GetUploadCommandPool(),
                         1, &commandBuffer_);
  }

  if (stagingBuffer_.IsValid()) {
    device_.DestroyBuffer(stagingBuffer_);
  }
}

void VulkanUploadContext::Begin() {
  head_ = 0;
  cmd_->Begin();
}

u64 VulkanUploadContext::Allocate(u64 size, u64 alignment) {
  u64 aligned = AlignUp(head_, alignment);
  if (aligned + size > capacity_) {
    throw std::runtime_error("UploadContext staging buffer overflow");
  }
  head_ = aligned + size;
  return aligned;
}

void VulkanUploadContext::UploadBuffer(const BufferUploadDesc &desc) {
  if (!desc.data || desc.size == 0) {
    return;
  }

  u64 offset = Allocate(desc.size, 16);
  std::memcpy(mappedPtr_ + offset, desc.data, desc.size);

  vmaFlushAllocation(device_.GetAllocator(),
                     device_.GetBuffer(stagingBuffer_).allocation, offset,
                     desc.size);

  cmd_->CopyBuffer(stagingBuffer_, desc.dstBuffer,
                   BufferCopyRegion{.srcOffset = offset,
                                    .dstOffset = desc.dstOffset,
                                    .size = desc.size});
}

void VulkanUploadContext::UploadImage(const ImageUploadDesc &desc,
                                      const void *data, u64 dataSize) {
  if (!data || dataSize == 0) {
    return;
  }

  u64 offset = Allocate(dataSize, 16);
  std::memcpy(mappedPtr_ + offset, data, dataSize);

  vmaFlushAllocation(device_.GetAllocator(),
                     device_.GetBuffer(stagingBuffer_).allocation, offset,
                     dataSize);

  cmd_->Barrier(ImageBarrier{
      .image = desc.dstImage,
      .oldLayout = desc.oldLayout,
      .newLayout = ImageLayout::TransferDst,
      .aspect = desc.aspect,
      .baseMipLevel = desc.mipLevel,
      .mipLevelCount = 1,
      .baseArrayLayer = desc.baseArrayLayer,
      .layerCount = desc.layerCount,
  });

  BufferImageCopyRegion region{};
  region.bufferOffset = offset;
  region.bufferRowLength = desc.bufferRowLength;
  region.bufferImageHeight = desc.bufferImageHeight;
  region.mipLevel = desc.mipLevel;
  region.baseArrayLayer = desc.baseArrayLayer;
  region.layerCount = desc.layerCount;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {desc.width, desc.height, desc.depth};
  region.aspect = desc.aspect;

  cmd_->CopyBufferToImage(stagingBuffer_, desc.dstImage, region);

  cmd_->Barrier(ImageBarrier{
      .image = desc.dstImage,
      .oldLayout = ImageLayout::TransferDst,
      .newLayout = desc.finalLayout,
      .aspect = desc.aspect,
      .baseMipLevel = desc.mipLevel,
      .mipLevelCount = 1,
      .baseArrayLayer = desc.baseArrayLayer,
      .layerCount = desc.layerCount,
  });
}

void VulkanUploadContext::Flush() {
  cmd_->End();

  VK_CHECK(vkResetFences(device_.GetVkDevice(), 1, &fence_),
           "Failed to reset upload fence");

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer_;

  VK_CHECK(vkQueueSubmit(device_.GetGraphicsQueue(), 1, &submitInfo, fence_),
           "Failed to submit upload command buffer");

  VK_CHECK(
      vkWaitForFences(device_.GetVkDevice(), 1, &fence_, VK_TRUE, UINT64_MAX),
      "Failed to wait for upload fence");
}

} // namespace Velos::RHI
