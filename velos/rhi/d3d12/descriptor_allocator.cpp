#include "descriptor_allocator.h"
#include <algorithm>

namespace Velos::D3D12 {

void CpuDescriptorAllocator::Initialize(ID3D12Device &device,
                                        D3D12_DESCRIPTOR_HEAP_TYPE type,
                                        u32 capacity) {
  if (capacity == 0) {
    throw std::invalid_argument("Descriptor heap capacity must be non-zero");
  }

  D3D12_DESCRIPTOR_HEAP_DESC desc{};
  desc.Type = type;
  desc.NumDescriptors = capacity;
  ThrowIfFailed(device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_)),
                "ID3D12Device::CreateDescriptorHeap");
  descriptorSize_ = device.GetDescriptorHandleIncrementSize(type);
  capacity_ = capacity;
  nextIndex_ = 0;
  freeIndices_.clear();
  freeIndices_.reserve(capacity);
}

CpuDescriptorAllocation CpuDescriptorAllocator::Allocate() {
  u32 index = 0;
  if (!freeIndices_.empty()) {
    index = freeIndices_.back();
    freeIndices_.pop_back();
  } else {
    if (nextIndex_ >= capacity_) {
      throw std::runtime_error("D3D12 CPU descriptor heap is exhausted");
    }
    index = nextIndex_++;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE handle =
      heap_->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(index) * descriptorSize_;
  return {.handle = handle, .index = index};
}

void CpuDescriptorAllocator::Free(CpuDescriptorAllocation allocation) {
  if (!allocation.IsValid() || allocation.index >= nextIndex_) {
    throw std::invalid_argument("Invalid D3D12 CPU descriptor allocation");
  }
  freeIndices_.push_back(allocation.index);
}

}  // namespace Velos::D3D12

namespace Velos::D3D12 {
void ShaderDescriptorArena::Initialize(ID3D12Device &device,
                                       D3D12_DESCRIPTOR_HEAP_TYPE type,
                                       u32 capacity) {
  const D3D12_DESCRIPTOR_HEAP_DESC desc{
      type, capacity, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  ThrowIfFailed(device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_)),
                "Create shader-visible descriptor heap");
  stride_ = device.GetDescriptorHandleIncrementSize(type);
  free_ = {{0, capacity}};
  free_.reserve(capacity);  // Snapshot destruction must not allocate while
                            // returning ranges.
}
u32 ShaderDescriptorArena::Allocate(u32 count) {
  if (!count)
    return UINT_MAX;
  for (auto it = free_.begin(); it != free_.end(); ++it)
    if (it->second >= count) {
      const u32 result = it->first;
      it->first += count;
      it->second -= count;
      if (!it->second)
        free_.erase(it);
      return result;
    }
  throw std::runtime_error(
      "Shader-visible descriptor heap exhausted; retire completed frames or "
      "reduce simultaneously bound arrays");
}
void ShaderDescriptorArena::Free(u32 first, u32 count) {
  if (!count)
    return;
  free_.push_back({first, count});
  std::sort(free_.begin(), free_.end());
  for (size_t i = 1; i < free_.size();) {
    if (free_[i - 1].first + free_[i - 1].second == free_[i].first) {
      free_[i - 1].second += free_[i].second;
      free_.erase(free_.begin() + i);
    } else
      ++i;
  }
}
D3D12_CPU_DESCRIPTOR_HANDLE ShaderDescriptorArena::Cpu(u32 index) const {
  auto handle = heap_->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += SIZE_T(index) * stride_;
  return handle;
}
D3D12_GPU_DESCRIPTOR_HANDLE ShaderDescriptorArena::Gpu(u32 index) const {
  auto handle = heap_->GetGPUDescriptorHandleForHeapStart();
  handle.ptr += UINT64(index) * stride_;
  return handle;
}
}  // namespace Velos::D3D12
