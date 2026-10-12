#include "descriptor_allocator.h"

namespace Velos::D3D12 {

void CpuDescriptorAllocator::Initialize(
    ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 capacity) {
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

} // namespace Velos::D3D12
