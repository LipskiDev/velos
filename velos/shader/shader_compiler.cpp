#include "shader_compiler.h"

#include <fstream>
#include <shaderc/shaderc.hpp>
#include <sstream>
#include <stdexcept>

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

  shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
      source, ToShadercKind(input.stage), input.path.c_str(),
      input.entryPoint.c_str(), options);

  if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
    throw std::runtime_error("Shader compilation failed for " + input.path +
                             ":\n" + result.GetErrorMessage());
  }

  ShaderCompileOutput output;
  output.spirv.assign(result.cbegin(), result.cend());
  return output;
}
} // namespace Velos
