#pragma once

#include "rhi/upload_context.h"
#include "rhi/vulkan/device.h"
namespace Velos::Vulkan {
using namespace Velos::RHI;

class UploadContext final : public IUploadContext {
public:
  UploadContext(Device &device, u64 size);
  ~UploadContext() override;

  void Begin() override;
  void UploadBuffer(const BufferUploadDesc &desc) override;
  void UploadImage(const ImageUploadDesc &desc, const void *data,
                   u64 dataSize) override;
  void Flush() override;

private:
  u64 Allocate(u64 size, u64 alignment);

private:
  Device &device_;

  BufferHandle stagingBuffer_{};
  u8 *mappedPtr_ = nullptr;
  u64 capacity_ = 0;
  u64 head_ = 0;

  VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
  std::unique_ptr<CommandList> cmd_;

  VkFence fence_ = VK_NULL_HANDLE;
};

} // namespace Velos::Vulkan
