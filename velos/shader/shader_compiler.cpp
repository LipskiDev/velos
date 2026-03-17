#include "shader_compiler.h"

#include <fstream>
#include <shaderc/shaderc.hpp>
#include <sstream>
#include <stdexcept>

#include <spirv_reflect.h>

namespace Velos {
namespace {
shaderc_shader_kind ToShadercKind(Velos::RHI::ShaderStage stage) {
  switch (stage) {
  case Velos::RHI::ShaderStage::Vertex:
    return shaderc_glsl_vertex_shader;
  case Velos::RHI::ShaderStage::Fragment:
    return shaderc_glsl_fragment_shader;
  case Velos::RHI::ShaderStage::Compute:
    return shaderc_glsl_compute_shader;
  default:
    throw std::runtime_error("Unsupported shader stage for shaderc");
  }
}
} // namespace

std::string ShaderCompiler::ReadTextFile(const std::string &path) {
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open shader file: " + path);
  }

  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

ShaderCompileOutput
ShaderCompiler::CompileFile(const ShaderCompileInput &input) {

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

ShaderReflectionData
ShaderCompiler::ReflectSpirv(const std::vector<uint32_t> &spirv,
                             RHI::ShaderStage stage) {
  ShaderReflectionData out{};
  out.stage = stage;

  SpvReflectShaderModule module{};
  SpvReflectResult result = spvReflectCreateShaderModule(
      spirv.size() * sizeof(u32), spirv.data(), &module);

  if (result != SPV_REFLECT_RESULT_SUCCESS) {
    throw std::runtime_error("Failed to reflect SPIR-V shader module");
  }

  out.entryPoint = module.entry_point_name ? module.entry_point_name : "main";

  // ReflectDescriptorBindings(module, stage, out);
  ReflectPushConstants(module, stage, out);

  spvReflectDestroyShaderModule(&module);
  return out;
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

ShaderCompileOutput
ShaderCompiler::CompileGlslToSpirv(const ShaderCompileInput &input) {
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

ShaderCompileOutput
ShaderCompiler::CompileHlslToSpirv(const ShaderCompileInput &input) {
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
}

ShaderCompileOutput
ShaderCompiler::CompileSlangToSpirv(const ShaderCompileInput &input) {
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
} // namespace Velos
