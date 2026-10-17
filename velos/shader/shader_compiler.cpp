#include "vlpch.h"
#include "shader_compiler.h"

#include <fstream>
#include <filesystem>
#include <shaderc/shaderc.hpp>
#include <sstream>
#include <stdexcept>
#include <cstring>
#if defined(VL_HAS_D3D12)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <dxcapi.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#endif

#include <spirv_reflect.h>

namespace Velos {
namespace {
class RelativeShaderIncluder final
    : public shaderc::CompileOptions::IncluderInterface {
  struct Include {
    shaderc_include_result result{};
    std::string name;
    std::string content;
  };

 public:
  shaderc_include_result *GetInclude(const char *requested,
                                     shaderc_include_type,
                                     const char *requesting,
                                     size_t depth) override {
    auto *include = new Include;
    const auto path =
        std::filesystem::path(requesting).parent_path() / requested;
    std::ifstream file(path, std::ios::binary);
    if (depth > 32 || !file) {
      include->content =
          "Cannot include shader file (missing file or excessive nesting): " +
          path.string();
    } else {
      include->name = path.lexically_normal().string();
      std::ostringstream source;
      source << file.rdbuf();
      include->content = source.str();
    }
    include->result.source_name = include->name.c_str();
    include->result.source_name_length = include->name.size();
    include->result.content = include->content.c_str();
    include->result.content_length = include->content.size();
    include->result.user_data = include;
    return &include->result;
  }
  void ReleaseInclude(shaderc_include_result *result) override {
    delete static_cast<Include *>(result->user_data);
  }
};

shaderc_shader_kind ToShadercKind(Velos::RHI::ShaderStage stage) {
  switch (stage) {
    case Velos::RHI::ShaderStage::Vertex:
      return shaderc_glsl_vertex_shader;
    case Velos::RHI::ShaderStage::Fragment:
      return shaderc_glsl_fragment_shader;
    case Velos::RHI::ShaderStage::Compute:
      return shaderc_glsl_compute_shader;
    case Velos::RHI::ShaderStage::Task:
      return shaderc_glsl_task_shader;
    case Velos::RHI::ShaderStage::Mesh:
      return shaderc_glsl_mesh_shader;
    default:
      throw std::runtime_error("Unsupported shader stage for shaderc");
  }
}
}  // namespace

RHI::BindingType ToBindingType(ShaderResourceType type) {
  switch (type) {
    case ShaderResourceType::UniformBuffer:
      return RHI::BindingType::UniformBuffer;
    case ShaderResourceType::StorageBuffer:
      return RHI::BindingType::StorageBuffer;
    case ShaderResourceType::SampledImage:
      return RHI::BindingType::SampledTexture;
    case ShaderResourceType::StorageImage:
      return RHI::BindingType::StorageImage;
    case ShaderResourceType::Sampler:
      return RHI::BindingType::Sampler;
    case ShaderResourceType::CombinedImageSampler:
      return RHI::BindingType::CombinedImageSampler;
    default:
      throw std::runtime_error("Unsupported reflected resource type");
  }
}

std::string ShaderCompiler::ReadTextFile(const std::string &path) {
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open shader file: " + path);
  }

  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

ShaderCompileOutput ShaderCompiler::CompileFile(
    const ShaderCompileInput &input) {
  if (input.outputFormat == ShaderBinaryFormat::Dxil) {
    if (input.language != ShaderSourceLanguage::HLSL)
      throw std::runtime_error("DXIL output requires HLSL input");
    return CompileHlslWithDxc(input);
  }

  ShaderCompileOutput output;

  switch (input.language) {
    case ShaderSourceLanguage::GLSL:
      output = CompileGlslToSpirv(input);
      break;

    case ShaderSourceLanguage::HLSL:
      output = CompileHlslToSpirv(input);
      break;

    case ShaderSourceLanguage::Slang:
      output = CompileSlangToSpirv(input);
      break;

    case ShaderSourceLanguage::SpirvBinary:
      output = LoadSpirvBinary(input.path);
      break;

    default:
      throw std::runtime_error("Unsupported ShaderSourceLanguage");
  }

  output.reflection = ReflectSpirv(output.spirv, input.stage);

  return output;
}

ShaderCompileOutput ShaderCompiler::CompileHlslWithDxc(
    const ShaderCompileInput &input) {
#if defined(VL_HAS_D3D12)
  using Microsoft::WRL::ComPtr;
  const auto check = [](HRESULT result) {
    if (FAILED(result))
      throw std::runtime_error("DXC operation failed");
  };
  const wchar_t *profile = nullptr;
  switch (input.stage) {
    case RHI::ShaderStage::Vertex:
      profile = L"vs_6_6";
      break;
    case RHI::ShaderStage::Fragment:
      profile = L"ps_6_6";
      break;
    case RHI::ShaderStage::Compute:
      profile = L"cs_6_6";
      break;
    case RHI::ShaderStage::Task:
      profile = L"as_6_6";
      break;
    case RHI::ShaderStage::Mesh:
      profile = L"ms_6_6";
      break;
    default:
      throw std::runtime_error("Unsupported DXIL shader stage");
  }
  ComPtr<IDxcUtils> utils;
  ComPtr<IDxcCompiler3> compiler;
  ComPtr<IDxcIncludeHandler> includes;
  check(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)));
  check(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)));
  check(utils->CreateDefaultIncludeHandler(&includes));
  const std::string source = ReadTextFile(input.path);
  const auto filename = std::filesystem::path(input.path).wstring();
  const auto directory =
      std::filesystem::absolute(input.path).parent_path().wstring();
  const std::wstring entry(input.entryPoint.begin(), input.entryPoint.end());
  const wchar_t *stageDefine = nullptr;
  switch (input.stage) {
    case RHI::ShaderStage::Vertex:
      stageDefine = L"-DIRYVEN_STAGE_VERTEX=1";
      break;
    case RHI::ShaderStage::Fragment:
      stageDefine = L"-DIRYVEN_STAGE_FRAGMENT=1";
      break;
    case RHI::ShaderStage::Compute:
      stageDefine = L"-DIRYVEN_STAGE_COMPUTE=1";
      break;
    case RHI::ShaderStage::Task:
      stageDefine = L"-DIRYVEN_STAGE_TASK=1";
      break;
    case RHI::ShaderStage::Mesh:
      stageDefine = L"-DIRYVEN_STAGE_MESH=1";
      break;
    default:
      throw std::runtime_error("Unsupported HLSL shader stage");
  }
  std::vector<const wchar_t *> arguments{
      filename.c_str(), L"-E", entry.c_str(),     L"-T",
      profile,          L"-I", directory.c_str(), stageDefine};
  if (input.outputFormat == ShaderBinaryFormat::Spirv) {
    arguments.insert(arguments.end(), {L"-spirv", L"-fspv-target-env=vulkan1.3",
                                       L"-fvk-use-gl-layout"});
  }
  const DxcBuffer buffer{source.data(), source.size(), DXC_CP_UTF8};
  ComPtr<IDxcResult> result;
  check(compiler->Compile(&buffer, arguments.data(),
                          static_cast<UINT32>(arguments.size()), includes.Get(),
                          IID_PPV_ARGS(&result)));
  HRESULT status;
  check(result->GetStatus(&status));
  if (FAILED(status)) {
    ComPtr<IDxcBlobUtf8> errors;
    check(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr));
    throw std::runtime_error(
        "HLSL compilation failed for " + input.path + ":\n" +
        (errors ? errors->GetStringPointer() : "Unknown DXC error"));
  }
  ComPtr<IDxcBlob> object;
  check(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr));
  ShaderCompileOutput output;
  if (input.outputFormat == ShaderBinaryFormat::Spirv) {
    if (object->GetBufferSize() % sizeof(uint32_t))
      throw std::runtime_error("Invalid DXC SPIR-V size");
    output.spirv.resize(object->GetBufferSize() / sizeof(uint32_t));
    std::memcpy(output.spirv.data(), object->GetBufferPointer(),
                object->GetBufferSize());
    return output;  // CompileFile reflects the SPIR-V module below.
  }
  output.dxil.resize(object->GetBufferSize());
  std::memcpy(output.dxil.data(), object->GetBufferPointer(),
              object->GetBufferSize());
  ComPtr<IDxcBlob> reflectionBlob;
  check(result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflectionBlob),
                          nullptr));
  const DxcBuffer reflectionBuffer{reflectionBlob->GetBufferPointer(),
                                   reflectionBlob->GetBufferSize(), 0};
  ComPtr<ID3D12ShaderReflection> reflection;
  check(utils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&reflection)));
  D3D12_SHADER_DESC description{};
  check(reflection->GetDesc(&description));
  output.reflection.entryPoint = input.entryPoint;
  output.reflection.stage = input.stage;
  for (UINT i = 0; i < description.BoundResources; ++i) {
    D3D12_SHADER_INPUT_BIND_DESC binding{};
    check(reflection->GetResourceBindingDesc(i, &binding));
    if (binding.Space == 0x7fffffff && binding.Type == D3D_SIT_CBUFFER) {
      if (binding.BindPoint != 0 || binding.BindCount != 1)
        throw std::runtime_error("Push constants must use b0, space2147483647");
      D3D12_SHADER_BUFFER_DESC constants{};
      check(reflection->GetConstantBufferByName(binding.Name)
                ->GetDesc(&constants));
      output.reflection.pushConstants.push_back(
          {0, constants.Size, input.stage});
      // Root constants cannot represent arrays, including nested struct
      // members.
      const auto hasArray = [&](auto &&self,
                                ID3D12ShaderReflectionType *type) -> bool {
        D3D12_SHADER_TYPE_DESC typeDescription{};
        check(type->GetDesc(&typeDescription));
        if (typeDescription.Elements)
          return true;
        for (UINT member = 0; member < typeDescription.Members; ++member)
          if (self(self, type->GetMemberTypeByIndex(member)))
            return true;
        return false;
      };
      auto *constantBuffer = reflection->GetConstantBufferByName(binding.Name);
      for (UINT variable = 0; variable < constants.Variables; ++variable)
        if (hasArray(hasArray,
                     constantBuffer->GetVariableByIndex(variable)->GetType()))
          output.reflection.rootConstantsCompatible = false;
      continue;
    }
    ShaderResourceBinding resource{};
    resource.name = binding.Name;
    resource.set = binding.Space;
    resource.binding = binding.BindPoint;
    resource.runtimeArray =
        binding.BindCount == 0 || binding.BindCount == UINT_MAX;
    resource.arraySize = resource.runtimeArray ? 1 : binding.BindCount;
    resource.stage = input.stage;
    resource.scalarType = binding.ReturnType == D3D_RETURN_TYPE_UINT
                              ? ShaderResourceScalarType::Unsigned
                          : binding.ReturnType == D3D_RETURN_TYPE_SINT
                              ? ShaderResourceScalarType::Signed
                              : ShaderResourceScalarType::Float;
    switch (binding.Type) {
      case D3D_SIT_CBUFFER:
        resource.type = ShaderResourceType::UniformBuffer;
        break;
      case D3D_SIT_SAMPLER:
        resource.type = ShaderResourceType::Sampler;
        break;
      case D3D_SIT_TEXTURE:
        if (binding.Dimension == D3D_SRV_DIMENSION_BUFFER)
          throw std::runtime_error(
              "Typed buffer views are not exposed by the RHI; use "
              "ByteAddressBuffer or StructuredBuffer");
        resource.type = ShaderResourceType::SampledImage;
        break;
      case D3D_SIT_STRUCTURED:
      case D3D_SIT_BYTEADDRESS:
        resource.type = ShaderResourceType::StorageBuffer;
        resource.readOnly = true;
        break;
      case D3D_SIT_UAV_RWSTRUCTURED:
      case D3D_SIT_UAV_RWBYTEADDRESS:
        resource.type = ShaderResourceType::StorageBuffer;
        break;
      case D3D_SIT_UAV_RWTYPED:
        if (binding.Dimension == D3D_SRV_DIMENSION_BUFFER)
          throw std::runtime_error(
              "Typed storage buffers are not exposed by the RHI");
        resource.type = ShaderResourceType::StorageImage;
        break;
      default:
        throw std::runtime_error(
            "Unsupported DXIL resource type (counter/append/acceleration "
            "resources are not exposed by this RHI)");
    }
    if (binding.Type == D3D_SIT_STRUCTURED ||
        binding.Type == D3D_SIT_UAV_RWSTRUCTURED)
      resource.structureStride = binding.NumSamples;
    switch (binding.Dimension) {
      case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
        resource.imageViewType = RHI::ImageViewType::View2DArray;
        break;
      case D3D_SRV_DIMENSION_TEXTURECUBE:
        resource.imageViewType = RHI::ImageViewType::Cube;
        break;
      case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
        resource.imageViewType = RHI::ImageViewType::CubeArray;
        break;
      default:
        break;
    }
    if ((resource.type == ShaderResourceType::SampledImage ||
         resource.type == ShaderResourceType::StorageImage) &&
        binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2D &&
        binding.Dimension != D3D_SRV_DIMENSION_TEXTURE2DARRAY &&
        binding.Dimension != D3D_SRV_DIMENSION_TEXTURECUBE &&
        binding.Dimension != D3D_SRV_DIMENSION_TEXTURECUBEARRAY)
      throw std::runtime_error(
          "This texture dimension/MSAA mode is not exposed by the RHI");
    output.reflection.resources.push_back(resource);
  }
  return output;
#else
  throw std::runtime_error(
      "DXIL compilation requires the Windows D3D12 backend");
#endif
}

ShaderReflectionData ShaderCompiler::ReflectSpirv(
    const std::vector<uint32_t> &spirv, RHI::ShaderStage stage) {
  ShaderReflectionData out{};
  out.stage = stage;

  SpvReflectShaderModule module{};
  SpvReflectResult result = spvReflectCreateShaderModule(
      spirv.size() * sizeof(u32), spirv.data(), &module);

  if (result != SPV_REFLECT_RESULT_SUCCESS) {
    throw std::runtime_error("Failed to reflect SPIR-V shader module");
  }

  out.entryPoint = module.entry_point_name ? module.entry_point_name : "main";

  ReflectDescriptorBindings(module, stage, out);
  ReflectPushConstants(module, stage, out);

  spvReflectDestroyShaderModule(&module);
  return out;
}

void ShaderCompiler::ReflectDescriptorBindings(
    const SpvReflectShaderModule &module, RHI::ShaderStage stage,
    ShaderReflectionData &out) {
  uint32_t bindingCount = 0;
  SpvReflectResult result =
      spvReflectEnumerateDescriptorBindings(&module, &bindingCount, nullptr);
  if (result != SPV_REFLECT_RESULT_SUCCESS) {
    throw std::runtime_error("Failed to enumerate descriptor bindings");
  }
  std::vector<SpvReflectDescriptorBinding *> bindings(bindingCount);
  result = spvReflectEnumerateDescriptorBindings(&module, &bindingCount,
                                                 bindings.data());
  if (result != SPV_REFLECT_RESULT_SUCCESS) {
    throw std::runtime_error("Failed to fetch descriptor bindings");
  }
  for (SpvReflectDescriptorBinding *binding : bindings) {
    ShaderResourceBinding resource{};
    resource.name = binding->name ? binding->name : "";
    resource.set = binding->set;
    resource.binding = binding->binding;
    resource.arraySize =
        binding->array.dims_count > 0 ? binding->array.dims[0] : 1;
    resource.runtimeArray =
        binding->array.dims_count > 0 && binding->array.dims[0] == 0;
    resource.stage = stage;
    switch (binding->descriptor_type) {
      case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        resource.type = ShaderResourceType::UniformBuffer;
        break;
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        resource.type = ShaderResourceType::StorageBuffer;
        break;
      case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        resource.type = ShaderResourceType::SampledImage;
        break;
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        resource.type = ShaderResourceType::StorageImage;
        break;
      case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
        resource.type = ShaderResourceType::Sampler;
        break;
      case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        resource.type = ShaderResourceType::CombinedImageSampler;
        break;
      case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        resource.type = ShaderResourceType::InputAttachment;
        break;
      default:
        throw std::runtime_error("Unsupported descriptor type in reflection");
    }
    out.resources.push_back(resource);
  }
}

void ShaderCompiler::ReflectPushConstants(const SpvReflectShaderModule &module,
                                          RHI::ShaderStage stage,
                                          ShaderReflectionData &out) {
  uint32_t blockCount = 0;
  SpvReflectResult result =
      spvReflectEnumeratePushConstantBlocks(&module, &blockCount, nullptr);

  if (result != SPV_REFLECT_RESULT_SUCCESS) {
    throw std::runtime_error("Failed to enumerate push constant blocks");
  }

  std::vector<SpvReflectBlockVariable *> blocks(blockCount);
  result = spvReflectEnumeratePushConstantBlocks(&module, &blockCount,
                                                 blocks.data());

  if (result != SPV_REFLECT_RESULT_SUCCESS) {
    throw std::runtime_error("Failed to fetch push constant blocks");
  }

  for (SpvReflectBlockVariable *block : blocks) {
    PushConstantRangeInfo range{};
    range.offset = block->offset;
    range.size = block->size;
    range.stage = stage;
    out.pushConstants.push_back(range);
  }
}

PipelineReflectionData ShaderCompiler::MergeShaderReflection(
    std::span<const ShaderReflectionData> shaders) {
  PipelineReflectionData pipelineReflection;
  for (const auto &shader : shaders) {
    // Merge resources
    for (const auto &resource : shader.resources) {
      auto it = std::find_if(
          pipelineReflection.resources.begin(),
          pipelineReflection.resources.end(),
          [&resource](const ShaderResourceBinding &existingResource) {
            return existingResource.set == resource.set &&
                   existingResource.binding == resource.binding;
          });
      if (it != pipelineReflection.resources.end()) {
        const bool combined =
            ((it->type == ShaderResourceType::CombinedImageSampler &&
              (resource.type == ShaderResourceType::SampledImage ||
               resource.type == ShaderResourceType::Sampler ||
               resource.type == ShaderResourceType::CombinedImageSampler)) ||
             ((it->type == ShaderResourceType::SampledImage &&
               resource.type == ShaderResourceType::Sampler) ||
              (it->type == ShaderResourceType::Sampler &&
               resource.type == ShaderResourceType::SampledImage)));
        if (combined)
          it->type = ShaderResourceType::CombinedImageSampler;
        else if (it->type != resource.type)
          throw std::runtime_error(
              "Conflicting shader resource types at one set/binding");
        if (it->structureStride != resource.structureStride &&
            it->type == ShaderResourceType::StorageBuffer)
          throw std::runtime_error("Conflicting structured-buffer strides");
        it->arraySize = std::max(it->arraySize, resource.arraySize);
        it->runtimeArray |= resource.runtimeArray;
        it->readOnly &= resource.readOnly;
        if (resource.type == ShaderResourceType::SampledImage) {
          it->imageViewType = resource.imageViewType;
          it->scalarType = resource.scalarType;
        }
        // Update stage flags if the resource already exists
        it->stage = static_cast<RHI::ShaderStage>(
            static_cast<int>(it->stage) | static_cast<int>(resource.stage));
      } else {
        // Add new resource
        pipelineReflection.resources.push_back(resource);
      }
    }
    // Merge push constants
    for (const auto &pushConstant : shader.pushConstants) {
      auto it = std::find_if(
          pipelineReflection.pushConstants.begin(),
          pipelineReflection.pushConstants.end(),
          [&pushConstant](const PushConstantRangeInfo &existingRange) {
            return existingRange.offset == pushConstant.offset &&
                   existingRange.size == pushConstant.size;
          });
      if (it != pipelineReflection.pushConstants.end()) {
        // Update stage flags if the push constant range already exists
        it->stage = static_cast<RHI::ShaderStage>(
            static_cast<int>(it->stage) | static_cast<int>(pushConstant.stage));
      } else {
        // Add new push constant range
        pipelineReflection.pushConstants.push_back(pushConstant);
      }
    }
  }
  return pipelineReflection;
}

ShaderCompileOutput ShaderCompiler::CompileGlslToSpirv(
    const ShaderCompileInput &input) {
  const std::string source = ReadTextFile(input.path);

  shaderc::Compiler compiler;
  shaderc::CompileOptions options;

#ifndef NDEBUG
  options.SetGenerateDebugInfo();
  options.SetOptimizationLevel(shaderc_optimization_level_zero);
#else
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
#endif

  options.SetTargetEnvironment(shaderc_target_env_vulkan,
                               shaderc_env_version_vulkan_1_3);

  options.SetSourceLanguage(shaderc_source_language_glsl);
  options.SetIncluder(std::make_unique<RelativeShaderIncluder>());

  auto result = compiler.CompileGlslToSpv(source, ToShadercKind(input.stage),
                                          input.path.c_str(),
                                          input.entryPoint.c_str(), options);

  if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
    throw std::runtime_error("GLSL compilation failed for " + input.path +
                             ":\n" + result.GetErrorMessage());
  }

  ShaderCompileOutput output;
  output.spirv.assign(result.cbegin(), result.cend());
  return output;
}

ShaderCompileOutput ShaderCompiler::CompileHlslToSpirv(
    const ShaderCompileInput &input) {
#if defined(VL_HAS_D3D12)
  return CompileHlslWithDxc(input);
#else
  const std::string source = ReadTextFile(input.path);

  shaderc::Compiler compiler;
  shaderc::CompileOptions options;

#ifndef NDEBUG
  options.SetGenerateDebugInfo();
  options.SetOptimizationLevel(shaderc_optimization_level_zero);
#else
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
#endif

  options.SetTargetEnvironment(shaderc_target_env_vulkan,
                               shaderc_env_version_vulkan_1_3);

  options.SetSourceLanguage(shaderc_source_language_hlsl);
  switch (input.stage) {
    case RHI::ShaderStage::Vertex:
      options.AddMacroDefinition("IRYVEN_STAGE_VERTEX", "1");
      break;
    case RHI::ShaderStage::Fragment:
      options.AddMacroDefinition("IRYVEN_STAGE_FRAGMENT", "1");
      break;
    case RHI::ShaderStage::Compute:
      options.AddMacroDefinition("IRYVEN_STAGE_COMPUTE", "1");
      break;
    case RHI::ShaderStage::Task:
      options.AddMacroDefinition("IRYVEN_STAGE_TASK", "1");
      break;
    case RHI::ShaderStage::Mesh:
      options.AddMacroDefinition("IRYVEN_STAGE_MESH", "1");
      break;
    default:
      throw std::runtime_error("Unsupported HLSL shader stage");
  }

  auto result = compiler.CompileGlslToSpv(source, ToShadercKind(input.stage),
                                          input.path.c_str(),
                                          input.entryPoint.c_str(), options);

  if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
    throw std::runtime_error("HLSL compilation failed for " + input.path +
                             ":\n" + result.GetErrorMessage());
  }

  ShaderCompileOutput output;
  output.spirv.assign(result.cbegin(), result.cend());
  return output;
#endif
}

ShaderCompileOutput ShaderCompiler::CompileSlangToSpirv(
    const ShaderCompileInput &input) {
  // later:
  // - create/load Slang session
  // - load module/source
  // - find entry point
  // - set target = SPIR-V
  // - compile
  // - return binary

  throw std::runtime_error("Slang compilation not implemented yet");
}

ShaderCompileOutput ShaderCompiler::LoadSpirvBinary(const std::string &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    throw std::runtime_error("Failed to open SPIR-V file: " + path);
  }

  const std::streamsize size = file.tellg();
  if (size < 0 || (size % 4) != 0) {
    throw std::runtime_error("Invalid SPIR-V file size: " + path);
  }

  file.seekg(0, std::ios::beg);

  ShaderCompileOutput output;
  output.spirv.resize(static_cast<size_t>(size) / 4);

  if (!file.read(reinterpret_cast<char *>(output.spirv.data()), size)) {
    throw std::runtime_error("Failed to read SPIR-V file: " + path);
  }

  return output;
}
}  // namespace Velos
