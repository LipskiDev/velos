#pragma once

#include "common.h"

#include <vector>

namespace Velos::D3D12 {

struct CpuDescriptorAllocation {
  D3D12_CPU_DESCRIPTOR_HANDLE handle{};
  u32 index = UINT32_MAX;

  bool IsValid() const noexcept { return index != UINT32_MAX; }
};

class CpuDescriptorAllocator {
 public:
  void Initialize(ID3D12Device &device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                  u32 capacity);

  CpuDescriptorAllocation Allocate();
  void Free(CpuDescriptorAllocation allocation);

 private:
  ComPtr<ID3D12DescriptorHeap> heap_;
  u32 descriptorSize_ = 0;
  u32 capacity_ = 0;
  u32 nextIndex_ = 0;
  std::vector<u32> freeIndices_;
};

class ShaderDescriptorArena {
 public:
  void Initialize(ID3D12Device &device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                  u32 capacity);
  u32 Allocate(u32 count);
  void Free(u32 first, u32 count);
  D3D12_CPU_DESCRIPTOR_HANDLE Cpu(u32 index) const;
  D3D12_GPU_DESCRIPTOR_HANDLE Gpu(u32 index) const;
  ID3D12DescriptorHeap *Heap() const { return heap_.Get(); }

 private:
  ComPtr<ID3D12DescriptorHeap> heap_;
  u32 stride_ = 0;
  std::vector<std::pair<u32, u32>> free_;
};

}  // namespace Velos::D3D12
