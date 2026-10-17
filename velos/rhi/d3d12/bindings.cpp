#include "device.h"
#include <algorithm>
#include <set>
#include <limits>

namespace Velos::D3D12 {
namespace {
BindingType Canonical(BindingType type) {
  return type == BindingType::StorageTexture ? BindingType::StorageImage : type;
}
bool SameBindings(const std::vector<BindingDesc> &a,
                  const std::vector<BindingDesc> &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].binding != b[i].binding ||
        Canonical(a[i].type) != Canonical(b[i].type) ||
        a[i].count != b[i].count || a[i].visibility != b[i].visibility ||
        a[i].flags != b[i].flags)
      return false;
  return true;
}
D3D12_DESCRIPTOR_RANGE_TYPE RangeType(const ShaderResourceBinding &resource) {
  switch (resource.type) {
    case ShaderResourceType::UniformBuffer:
      return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    case ShaderResourceType::Sampler:
      return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    case ShaderResourceType::StorageImage:
      return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    case ShaderResourceType::StorageBuffer:
      return resource.readOnly ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
                               : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    default:
      return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  }
}
}  // namespace

BindingLayoutHandle Device::CreateBindingLayout(const BindingLayoutDesc &desc) {
  if (desc.bindingCount && !desc.bindings)
    throw std::invalid_argument("Missing layout bindings");
  BindingLayout layout;
  u32 variableCount = 0;
  for (u32 i = 0; i < desc.bindingCount; ++i) {
    auto binding = desc.bindings[i];
    if (!binding.count || binding.count > 1000000 ||
        static_cast<u32>(binding.type) >
            static_cast<u32>(BindingType::StorageTexture) ||
        binding.visibility == ShaderStage::None ||
        (static_cast<u32>(binding.visibility) & ~31u) ||
        (static_cast<u32>(binding.flags) & ~7u))
      throw std::invalid_argument("Invalid binding layout entry");
    variableCount +=
        HasFlag(binding.flags, BindingFlags::VariableCount) ? 1 : 0;
    layout.bindings.push_back(binding);
  }
  std::sort(layout.bindings.begin(), layout.bindings.end(),
            [](auto a, auto b) { return a.binding < b.binding; });
  for (size_t i = 0; i < layout.bindings.size(); ++i) {
    if (i && layout.bindings[i - 1].binding == layout.bindings[i].binding)
      throw std::invalid_argument("Duplicate layout binding");
    if (HasFlag(layout.bindings[i].flags, BindingFlags::VariableCount) &&
        i + 1 != layout.bindings.size())
      throw std::invalid_argument(
          "Variable descriptor count must be the highest binding");
  }
  if (variableCount > 1)
    throw std::invalid_argument(
        "Only one variable-count binding is supported per set");
  const BindingLayoutHandle handle{nextBindingLayoutHandle_++};
  bindingLayouts_.emplace(handle.id, std::move(layout));
  return handle;
}
void Device::DestroyBindingLayout(BindingLayoutHandle handle) {
  bindingLayouts_.erase(handle.id);
}
const BindingLayout &Device::GetBindingLayout(
    BindingLayoutHandle handle) const {
  return bindingLayouts_.at(handle.id);
}

BindingPoolHandle Device::CreateBindingPool(const BindingPoolDesc &desc) {
  if (!desc.maxSets || (desc.poolSizeCount && !desc.poolSizes) ||
      (static_cast<u32>(desc.flags) & ~1u))
    throw std::invalid_argument("Invalid binding pool");
  BindingPool pool;
  pool.remainingSets = desc.maxSets;
  pool.flags = desc.flags;
  for (u32 i = 0; i < desc.poolSizeCount; ++i) {
    const auto &size = desc.poolSizes[i];
    if (!size.count || static_cast<u32>(size.type) >
                           static_cast<u32>(BindingType::StorageTexture))
      throw std::invalid_argument("Invalid pool descriptor budget");
    auto &remaining = pool.remaining[static_cast<u32>(Canonical(size.type))];
    if (size.count > UINT_MAX - remaining)
      throw std::invalid_argument("Pool descriptor budget overflow");
    remaining += size.count;
  }
  const BindingPoolHandle handle{nextBindingPoolHandle_++};
  bindingPools_.emplace(handle.id, std::move(pool));
  return handle;
}
void Device::DestroyBindingPool(BindingPoolHandle handle) {
  if (!bindingPools_.contains(handle.id))
    throw std::invalid_argument("Invalid binding pool");
  for (auto it = bindingSets_.begin(); it != bindingSets_.end();)
    if (it->second->pool.id == handle.id)
      it = bindingSets_.erase(it);
    else
      ++it;
  bindingPools_.erase(handle.id);  // Recorded/in-flight snapshots retain their
                                   // descriptors and resources.
}
const BindingPool &Device::GetBindingPool(BindingPoolHandle handle) const {
  return bindingPools_.at(handle.id);
}
const BindingSet &Device::GetBindingSet(BindingSetHandle handle) const {
  return *bindingSets_.at(handle.id);
}

BindingSetHandle Device::AllocateBindingSet(
    const BindingSetAllocationDesc &desc) {
  auto &pool = bindingPools_.at(desc.pool.id);
  const auto &layout = GetBindingLayout(desc.layout);
  if (!pool.remainingSets)
    throw std::runtime_error("Binding pool maxSets exhausted");
  auto set = std::make_shared<BindingSet>();
  set->pool = desc.pool;
  set->bindings = layout.bindings;
  auto remaining = pool.remaining;
  bool variable = false;
  for (const auto &binding : layout.bindings) {
    u32 count = binding.count;
    if (HasFlag(binding.flags, BindingFlags::UpdateAfterBind) &&
        !HasFlag(pool.flags, BindingPoolFlags::UpdateAfterBind))
      throw std::invalid_argument(
          "UpdateAfterBind layout requires an UpdateAfterBind pool");
    if (HasFlag(binding.flags, BindingFlags::VariableCount)) {
      variable = true;
      count = desc.variableBindingCount;
      if (count > binding.count)
        throw std::invalid_argument(
            "Variable binding count exceeds layout maximum");
    }
    auto &available = remaining[static_cast<u32>(Canonical(binding.type))];
    if (count > available)
      throw std::runtime_error("Binding pool descriptor budget exhausted");
    available -= count;
    set->values[binding.binding].resize(count);
  }
  if (!variable && desc.variableBindingCount)
    throw std::invalid_argument("Variable count provided for a fixed layout");
  const BindingSetHandle handle{nextBindingSetHandle_++};
  bindingSets_.emplace(handle.id, std::move(set));
  pool.remaining = std::move(remaining);
  --pool.remainingSets;
  return handle;
}

void Device::UpdateBindingSet(const BindingWriteDesc &desc) {
  auto &set = *bindingSets_.at(desc.dstSet.id);
  const auto found =
      std::find_if(set.bindings.begin(), set.bindings.end(),
                   [&](auto b) { return b.binding == desc.binding; });
  if (found == set.bindings.end() ||
      Canonical(found->type) != Canonical(desc.type) || !desc.descriptorCount)
    throw std::invalid_argument(
        "Descriptor write type/binding is incompatible with the layout");
  auto &values = set.values.at(desc.binding);
  if (desc.arrayElement > values.size() ||
      desc.descriptorCount > values.size() - desc.arrayElement)
    throw std::out_of_range("Descriptor write exceeds allocated array size");
  const auto type = Canonical(desc.type);
  const bool buffer =
      type == BindingType::UniformBuffer || type == BindingType::StorageBuffer;
  if ((buffer && !desc.bufferInfo) || (!buffer && !desc.imageInfo))
    throw std::invalid_argument("Missing descriptor write data");
  std::vector<BindingValue> writes;
  for (u32 i = 0; i < desc.descriptorCount; ++i) {
    BindingValue value;
    value.initialized = true;
    if (buffer) {
      const auto &info = desc.bufferInfo[i];
      const auto &resource = GetBuffer(info.buffer);
      if (info.offset >= resource.size)
        throw std::out_of_range("Buffer descriptor offset exceeds buffer");
      const u64 range = info.range ? info.range : resource.size - info.offset;
      if (!range || range > resource.size - info.offset)
        throw std::out_of_range("Buffer descriptor range exceeds buffer");
      if (!HasFlag(resource.usage, type == BindingType::UniformBuffer
                                       ? BufferUsage::Uniform
                                       : BufferUsage::Storage))
        throw std::invalid_argument(
            "Buffer usage does not match descriptor type");
      if (type == BindingType::UniformBuffer &&
          (info.offset % 256 || range > 65536 ||
           ((range + 255) & ~255ull) > resource.allocationSize - info.offset))
        throw std::invalid_argument(
            "CBV requires a 256-byte-aligned offset and at most 64KB of padded "
            "storage");
      if (type == BindingType::StorageBuffer && (info.offset % 4 || range % 4))
        throw std::invalid_argument(
            "Storage buffer ranges must be four-byte aligned");
      value.owner = resource.ownedResource;
      value.offset = info.offset;
      value.range = range;
      value.hasSrv = true;
      value.hasUav = resource.heapType == D3D12_HEAP_TYPE_DEFAULT;
    } else {
      const auto &info = desc.imageInfo[i];
      if (type != BindingType::Sampler) {
        const auto &view = GetImageView(info.imageView);
        const auto &image = GetImage(view.image);
        value.owner = image.resourcePtr;
        value.hasSrv = view.srv.IsValid();
        value.hasUav = view.uav.IsValid();
        value.srv = view.srvDescription;
        value.uav = view.uavDescription;
        if ((type == BindingType::StorageImage && !value.hasUav) ||
            (type != BindingType::StorageImage && !value.hasSrv))
          throw std::invalid_argument(
              "Image view lacks required shader descriptor");
        if (type == BindingType::StorageImage &&
            info.imageLayout != ImageLayout::General)
          throw std::invalid_argument(
              "Storage image bindings require General layout");
      }
      if (type == BindingType::Sampler ||
          type == BindingType::CombinedImageSampler) {
        value.sampler = GetSampler(info.sampler).description;
        value.hasSampler = true;
      }
    }
    writes.push_back(std::move(value));
  }
  // Validate the complete write before replacing any entries.
  std::move(writes.begin(), writes.end(), values.begin() + desc.arrayElement);
  ++set.revision;
}

ComPtr<ID3DBlob> Device::BuildRootSignature(
    Pipeline &pipeline, const PipelineLayoutDesc &desc,
    std::span<const Shader *const> shaders) {
  if (desc.descriptorSetLayoutCount > 32 ||
      (desc.descriptorSetLayoutCount && !desc.descriptorSetLayouts))
    throw std::invalid_argument("Invalid pipeline descriptor layout array");
  std::vector<ShaderResourceBinding> reflected;
  std::array<bool, 5> inlineCompatible{true, true, true, true, true};
  for (auto *shader : shaders)
    if (shader) {
      reflected.insert(reflected.end(), shader->reflection.resources.begin(),
                       shader->reflection.resources.end());
      for (auto range : shader->reflection.pushConstants) {
        if (range.offset % 4 || range.size % 4 || range.offset > 65536 ||
            range.size > 65536 - range.offset)
          throw std::invalid_argument("Invalid push constant range");
        pipeline.pushSize =
            std::max(pipeline.pushSize, range.offset + range.size);
        pipeline.pushStages = pipeline.pushStages | range.stage;
        for (u32 stage = 0; stage < 5; ++stage)
          if ((static_cast<u32>(range.stage) & (1u << stage)) != 0) {
            pipeline.pushSizes[stage] =
                std::max(pipeline.pushSizes[stage], range.offset + range.size);
            inlineCompatible[stage] =
                inlineCompatible[stage] &&
                shader->reflection.rootConstantsCompatible;
          }
      }
    }
  pipeline.sets.resize(desc.descriptorSetLayoutCount);
  std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> ranges;
  std::vector<D3D12_ROOT_PARAMETER> parameters;
  ranges.reserve(desc.descriptorSetLayoutCount * 2);
  for (u32 set = 0; set < desc.descriptorSetLayoutCount; ++set) {
    auto &target = pipeline.sets[set];
    target.bindings = GetBindingLayout(desc.descriptorSetLayouts[set]).bindings;
    for (const auto &binding : target.bindings) {
      std::vector<TableEntry> entries;
      for (const auto &resource : reflected)
        if (resource.set == set && resource.binding == binding.binding) {
          const auto reflectedType = Canonical(ToBindingType(resource.type)),
                     declared = Canonical(binding.type);
          const bool combined = declared == BindingType::CombinedImageSampler &&
                                (reflectedType == BindingType::SampledTexture ||
                                 reflectedType == BindingType::Sampler);
          if ((!combined && declared != reflectedType) ||
              (binding.visibility & resource.stage) != resource.stage ||
              (!resource.runtimeArray && resource.arraySize > binding.count))
            throw std::invalid_argument(
                "Pipeline layout does not cover shader reflection");
          const auto rangeType = RangeType(resource);
          auto previous =
              std::find_if(entries.begin(), entries.end(),
                           [&](auto e) { return e.type == rangeType; });
          if (previous != entries.end()) {
            if (previous->stride != resource.structureStride ||
                previous->imageType != resource.imageViewType ||
                previous->scalarType != resource.scalarType)
              throw std::invalid_argument(
                  "Conflicting resource shapes across shader stages");
            previous->requiredCount =
                std::max(previous->requiredCount, resource.arraySize);
            previous->runtimeArray |= resource.runtimeArray;
          } else
            entries.push_back({binding, rangeType, 0, resource.structureStride,
                               true, resource.imageViewType, resource.arraySize,
                               resource.runtimeArray, resource.scalarType});
        }
      if (Canonical(binding.type) == BindingType::StorageBuffer &&
          entries.size() > 1)
        throw std::invalid_argument(
            "Use a consistent HLSL SRV/UAV access declaration for a storage "
            "binding across stages");
      if (entries.empty()) {
        ShaderResourceBinding fallback{};
        switch (Canonical(binding.type)) {
          case BindingType::UniformBuffer:
            fallback.type = ShaderResourceType::UniformBuffer;
            break;
          case BindingType::StorageBuffer:
            fallback.type = ShaderResourceType::StorageBuffer;
            break;
          case BindingType::StorageImage:
            fallback.type = ShaderResourceType::StorageImage;
            break;
          case BindingType::Sampler:
            fallback.type = ShaderResourceType::Sampler;
            break;
          default:
            fallback.type = ShaderResourceType::SampledImage;
            break;
        }
        entries.push_back({binding, RangeType(fallback)});
        if (binding.type == BindingType::CombinedImageSampler)
          entries.push_back({binding, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER});
      }
      for (auto entry : entries) {
        auto &count = entry.type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
                          ? target.samplers
                          : target.resources;
        if (entry.binding.count > 1000000 - count)
          throw std::invalid_argument("Descriptor table is too large");
        entry.offset = count;
        count += entry.binding.count;
        target.entries.push_back(entry);
      }
    }
    for (bool samplers : {false, true}) {
      std::vector<D3D12_DESCRIPTOR_RANGE> table;
      for (const auto &entry : target.entries)
        if ((entry.type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER) == samplers)
          table.push_back({entry.type,
                           entry.runtimeArray ? UINT_MAX : entry.binding.count,
                           entry.binding.binding, set, entry.offset});
      if (table.empty())
        continue;
      ranges.push_back(std::move(table));
      (samplers ? target.samplerRoot : target.resourceRoot) =
          static_cast<u32>(parameters.size());
      D3D12_ROOT_PARAMETER parameter{};
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameter.DescriptorTable = {static_cast<UINT>(ranges.back().size()),
                                   ranges.back().data()};
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      parameters.push_back(parameter);
    }
  }
  for (const auto &resource : reflected) {
    if (resource.set >= pipeline.sets.size() ||
        !std::any_of(pipeline.sets[resource.set].bindings.begin(),
                     pipeline.sets[resource.set].bindings.end(),
                     [&](auto b) { return b.binding == resource.binding; }))
      throw std::invalid_argument(
          "Shader resource is missing from pipeline layout");
  }
  u32 rootDwords = static_cast<u32>(parameters.size());
  for (u32 stage = 0; stage < 5; ++stage) {
    if (!pipeline.pushSizes[stage])
      continue;
    pipeline.pushInline[stage] = inlineCompatible[stage];
    rootDwords +=
        pipeline.pushInline[stage] ? pipeline.pushSizes[stage] / 4 : 2;
  }
  // Preserve independent stage values. Spill the largest blocks only when the
  // descriptor tables and inline constants cannot fit in the root signature.
  while (rootDwords > 64) {
    u32 largestStage = 5;
    u32 largestDwords = 2;
    for (u32 stage = 0; stage < 5; ++stage)
      if (pipeline.pushInline[stage] &&
          pipeline.pushSizes[stage] / 4 > largestDwords) {
        largestStage = stage;
        largestDwords = pipeline.pushSizes[stage] / 4;
      }
    if (largestStage == 5)
      throw std::invalid_argument("Root signature exceeds the 64-DWORD budget");
    pipeline.pushInline[largestStage] = false;
    rootDwords -= largestDwords - 2;
  }
  const D3D12_SHADER_VISIBILITY visibility[]{
      D3D12_SHADER_VISIBILITY_VERTEX, D3D12_SHADER_VISIBILITY_PIXEL,
      D3D12_SHADER_VISIBILITY_ALL, D3D12_SHADER_VISIBILITY_AMPLIFICATION,
      D3D12_SHADER_VISIBILITY_MESH};
  for (u32 stage = 0; stage < 5; ++stage)
    if (pipeline.pushSizes[stage]) {
      pipeline.pushRoots[stage] = static_cast<u32>(parameters.size());
      D3D12_ROOT_PARAMETER parameter{};
      if (pipeline.pushInline[stage]) {
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants = {0, 0x7fffffff, pipeline.pushSizes[stage] / 4};
      } else {
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameter.Descriptor = {0, 0x7fffffff};
      }
      parameter.ShaderVisibility = visibility[stage];
      parameters.push_back(parameter);
    }
  D3D12_ROOT_SIGNATURE_DESC root{
      static_cast<UINT>(parameters.size()), parameters.data(), 0, nullptr,
      pipeline.compute
          ? D3D12_ROOT_SIGNATURE_FLAG_NONE
          : D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
  ComPtr<ID3DBlob> signature, errors;
  const auto status = D3D12SerializeRootSignature(
      &root, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
  if (FAILED(status) && errors)
    throw std::invalid_argument(
        static_cast<const char *>(errors->GetBufferPointer()));
  ThrowIfFailed(status, "Serialize root signature");
  ThrowIfFailed(
      device_->CreateRootSignature(0, signature->GetBufferPointer(),
                                   signature->GetBufferSize(),
                                   IID_PPV_ARGS(&pipeline.rootSignature)),
      "Create root signature");
  return signature;
}

GeneratedPipelineLayout Device::BuildPipelineLayout(
    const PipelineReflectionData &reflection,
    const PipelineLayoutOverrides &overrides) {
  GeneratedPipelineLayout result;
  if (reflection.resources.empty() && overrides.existingSetLayouts.empty())
    return result;
  u32 highest = 0;
  for (auto &r : reflection.resources)
    highest = std::max(highest, r.set);
  for (auto [set, layout] : overrides.existingSetLayouts)
    highest = std::max(highest, set);
  if (highest >= 32)
    throw std::invalid_argument(
        "Reflected set index exceeds root-layout capacity");
  result.setLayouts.resize(highest + 1);
  try {
    for (u32 set = 0; set <= highest; ++set) {
      std::vector<BindingDesc> bindings;
      for (const auto &resource : reflection.resources)
        if (resource.set == set) {
          if (resource.runtimeArray &&
              !overrides.existingSetLayouts.contains(set))
            throw std::invalid_argument(
                "Runtime descriptor arrays require an existing layout with an "
                "explicit maximum");
          bindings.push_back({resource.binding, ToBindingType(resource.type),
                              resource.arraySize, resource.stage,
                              BindingFlags::None});
        }
      if (auto found = overrides.existingSetLayouts.find(set);
          found != overrides.existingSetLayouts.end()) {
        const auto &existing = GetBindingLayout(found->second);
        for (auto expected : bindings) {
          const auto match = std::find_if(
              existing.bindings.begin(), existing.bindings.end(),
              [&](auto b) { return b.binding == expected.binding; });
          if (match == existing.bindings.end() ||
              Canonical(match->type) != Canonical(expected.type) ||
              match->count < expected.count ||
              (match->visibility & expected.visibility) != expected.visibility)
            throw std::invalid_argument(
                "Layout override does not cover reflection");
        }
        result.setLayouts[set] = found->second;
      } else {
        auto handle = CreateBindingLayout(
            {bindings.data(), static_cast<u32>(bindings.size())});
        result.setLayouts[set] = handle;
        result.ownedSetLayouts.push_back(handle);
      }
    }
  } catch (...) {
    for (auto handle : result.ownedSetLayouts)
      DestroyBindingLayout(handle);
    throw;
  }
  return result;
}

std::shared_ptr<DescriptorSnapshot> Device::SnapshotBindings(
    const PipelineSetLayout &layout, BindingSetHandle handle) {
  auto set = bindingSets_.at(handle.id);
  if (!SameBindings(layout.bindings, set->bindings))
    throw std::invalid_argument(
        "Binding set is incompatible with pipeline set layout");
  auto snapshot = std::shared_ptr<DescriptorSnapshot>(
      new DescriptorSnapshot, [this](DescriptorSnapshot *value) {
        shaderResources_.Free(
            value->resourceStart,
            value->resourceStart == UINT_MAX ? 0 : value->layout.resources);
        shaderSamplers_.Free(
            value->samplerStart,
            value->samplerStart == UINT_MAX ? 0 : value->layout.samplers);
        delete value;
      });
  snapshot->set = std::move(set);
  snapshot->layout = layout;
  snapshot->values = snapshot->set->values;
  snapshot->resourceStart = shaderResources_.Allocate(layout.resources);
  snapshot->samplerStart = shaderSamplers_.Allocate(layout.samplers);
  snapshot->deferred = std::any_of(
      layout.bindings.begin(), layout.bindings.end(),
      [](auto b) { return HasFlag(b.flags, BindingFlags::UpdateAfterBind); });
  if (!snapshot->deferred)
    PopulateSnapshot(*snapshot);
  return snapshot;
}

void Device::PopulateSnapshot(DescriptorSnapshot &snapshot) {
  snapshot.keepAlive.clear();
  for (const auto &entry : snapshot.layout.entries) {
    const auto &values =
        HasFlag(entry.binding.flags, BindingFlags::UpdateAfterBind)
            ? snapshot.set->values.at(entry.binding.binding)
            : snapshot.values.at(entry.binding.binding);
    for (u32 i = 0; i < entry.binding.count; ++i) {
      const BindingValue *value =
          i < values.size() && values[i].initialized ? &values[i] : nullptr;
      if (!value && i < values.size() && entry.used &&
          (entry.runtimeArray || i < entry.requiredCount) &&
          !HasFlag(entry.binding.flags, BindingFlags::PartiallyBound))
        throw std::invalid_argument(
            "Required descriptor array element is uninitialized");
      const bool sampler = entry.type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
      const auto nullFormat =
          entry.scalarType == ShaderResourceScalarType::Unsigned
              ? DXGI_FORMAT_R32G32B32A32_UINT
          : entry.scalarType == ShaderResourceScalarType::Signed
              ? DXGI_FORMAT_R32G32B32A32_SINT
              : DXGI_FORMAT_R32G32B32A32_FLOAT;
      const auto destination =
          sampler
              ? shaderSamplers_.Cpu(snapshot.samplerStart + entry.offset + i)
              : shaderResources_.Cpu(snapshot.resourceStart + entry.offset + i);
      if (value)
        snapshot.keepAlive.push_back(*value);
      if (sampler) {
        D3D12_SAMPLER_DESC desc{};
        desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        desc.AddressU = desc.AddressV = desc.AddressW =
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        desc.MaxAnisotropy = 1;
        desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        if (value) {
          if (!value->hasSampler)
            throw std::invalid_argument("Missing sampler descriptor");
          desc = value->sampler;
        }
        device_->CreateSampler(&desc, destination);
        continue;
      }
      if (entry.type == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
        if (value) {
          const D3D12_CONSTANT_BUFFER_VIEW_DESC desc{
              value->owner->GetGPUVirtualAddress() + value->offset,
              static_cast<UINT>((value->range + 255) & ~255ull)};
          device_->CreateConstantBufferView(&desc, destination);
        } else
          device_->CreateConstantBufferView(nullptr, destination);
        continue;
      }
      const bool buffer =
          Canonical(entry.binding.type) == BindingType::StorageBuffer;
      if (entry.type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV) {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (buffer) {
          desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
          desc.Format =
              entry.stride ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
          desc.Buffer.StructureByteStride = entry.stride;
          desc.Buffer.Flags = entry.stride ? D3D12_BUFFER_SRV_FLAG_NONE
                                           : D3D12_BUFFER_SRV_FLAG_RAW;
          if (value && (value->offset % (entry.stride ? entry.stride : 4) ||
                        value->range % (entry.stride ? entry.stride : 4)))
            throw std::invalid_argument(
                "Storage buffer offset/range does not match reflected element "
                "stride");
          desc.Buffer.FirstElement =
              value ? value->offset / (entry.stride ? entry.stride : 4) : 0;
          desc.Buffer.NumElements =
              value ? static_cast<UINT>(value->range /
                                        (entry.stride ? entry.stride : 4))
                    : 1;
        } else if (value) {
          if (!value->hasSrv)
            throw std::invalid_argument("Missing texture SRV");
          desc = value->srv;
          const bool shape =
              entry.imageType == ImageViewType::View2D
                  ? desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D
              : entry.imageType == ImageViewType::View2DArray
                  ? desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY
              : entry.imageType == ImageViewType::Cube
                  ? desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE
                  : desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
          const auto scalar = desc.Format == DXGI_FORMAT_X24_TYPELESS_G8_UINT
                                  ? ShaderResourceScalarType::Unsigned
                                  : ShaderResourceScalarType::Float;
          if (entry.used && (!shape || scalar != entry.scalarType))
            throw std::invalid_argument(
                "Texture SRV dimension/scalar type does not match HLSL "
                "reflection");
        } else {
          desc.Format = nullFormat;
          switch (entry.imageType) {
            case ImageViewType::View2D:
              desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
              desc.Texture2D.MipLevels = 1;
              break;
            case ImageViewType::View2DArray:
              desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
              desc.Texture2DArray.MipLevels = 1;
              desc.Texture2DArray.ArraySize = 1;
              break;
            case ImageViewType::Cube:
              desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
              desc.TextureCube.MipLevels = 1;
              break;
            case ImageViewType::CubeArray:
              desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
              desc.TextureCubeArray.MipLevels = 1;
              desc.TextureCubeArray.NumCubes = 1;
              break;
          }
        }
        device_->CreateShaderResourceView(value ? value->owner.Get() : nullptr,
                                          &desc, destination);
      } else {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        if (buffer) {
          desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
          desc.Format =
              entry.stride ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
          desc.Buffer.StructureByteStride = entry.stride;
          desc.Buffer.Flags = entry.stride ? D3D12_BUFFER_UAV_FLAG_NONE
                                           : D3D12_BUFFER_UAV_FLAG_RAW;
          if (value && (!value->hasUav ||
                        value->offset % (entry.stride ? entry.stride : 4) ||
                        value->range % (entry.stride ? entry.stride : 4)))
            throw std::invalid_argument(
                "Buffer cannot provide the reflected UAV/stride");
          desc.Buffer.FirstElement =
              value ? value->offset / (entry.stride ? entry.stride : 4) : 0;
          desc.Buffer.NumElements =
              value ? static_cast<UINT>(value->range /
                                        (entry.stride ? entry.stride : 4))
                    : 1;
        } else if (value) {
          if (!value->hasUav)
            throw std::invalid_argument("Missing texture UAV");
          desc = value->uav;
          const bool shape =
              entry.imageType == ImageViewType::View2D
                  ? desc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2D
                  : desc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
          if (entry.used &&
              (!shape || entry.scalarType != ShaderResourceScalarType::Float))
            throw std::invalid_argument(
                "Texture UAV dimension/scalar type does not match HLSL "
                "reflection");
        } else {
          desc.Format = nullFormat;
          desc.ViewDimension = entry.imageType == ImageViewType::View2D
                                   ? D3D12_UAV_DIMENSION_TEXTURE2D
                                   : D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
          if (desc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2DARRAY)
            desc.Texture2DArray.ArraySize = 1;
        }
        device_->CreateUnorderedAccessView(value ? value->owner.Get() : nullptr,
                                           nullptr, &desc, destination);
      }
    }
  }
  snapshot.deferred = false;
  snapshot.values.clear();
  snapshot.set.reset();
}
}  // namespace Velos::D3D12
