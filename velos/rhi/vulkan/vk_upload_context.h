#pragma once

#include "rhi/rhi_upload_context.h"
#include "rhi/vulkan/vk_device.h"
namespace Velos::RHI {

class VulkanUploadContext final : public IUploadContext {
public:
  VulkanUploadContext(VulkanDevice &device, u64 size);
  ~VulkanUploadContext() override;

  void Begin() override;
  void UploadBuffer(const BufferUploadDesc &desc) override;
  void UploadImage(const ImageUploadDesc &desc, const void *data,
                   u64 dataSize) override;
  void Flush() override;

private:
  u64 Allocate(u64 size, u64 alignment);

private:
  VulkanDevice &device_;

  BufferHandle stagingBuffer_{};
  u8 *mappedPtr_ = nullptr;
  u64 capacity_ = 0;
  u64 head_ = 0;

  VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
  std::unique_ptr<VulkanCommandList> cmd_;

  VkFence fence_ = VK_NULL_HANDLE;
};

} // namespace Velos::RHI
