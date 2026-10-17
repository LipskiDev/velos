#pragma once

#include <rhi/upload_context.h>
#include <rhi/d3d12/device.h>

namespace Velos::D3D12 {
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
  std::vector<PendingBufferAcquire> TakePendingBufferAcquires() override;
  std::vector<PendingImageAcquire> TakePendingImageAcquires() override;

 private:
  u64 Allocate(u64 size, u64 alignment);

 private:
  Device &device_;

  BufferHandle stagingBuffer_{};
  std::vector<BufferHandle> retiredStagingBuffers_;
  u8 *mappedPtr_ = nullptr;
  u64 capacity_ = 0;
  u64 head_ = 0;
  u64 fenceValue_ = 0;

  ComPtr<ID3D12CommandAllocator> commandAllocator_;
  ComPtr<ID3D12GraphicsCommandList> commandBuffer_;
  std::unique_ptr<CommandList> cmd_;

  ComPtr<ID3D12Fence> fence_;
  std::vector<PendingBufferAcquire> pendingBufferAcquires_;
  std::vector<PendingImageAcquire> pendingImageAcquires_;
};

}  // namespace Velos::D3D12
