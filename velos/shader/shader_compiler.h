#pragma once

#include "rhi/rhi_types.h"
#include <string>
#include <vector>
namespace Velos {
enum class ShaderSourceLanguage {
  GLSL,
  // HLSL
};

struct ShaderCompileInput {
  std::string path;
  Velos::RHI::ShaderStage stage = Velos::RHI::ShaderStage::None;
  ShaderSourceLanguage language = ShaderSourceLanguage::GLSL;
  std::string entryPoint = "main";
};

struct ShaderCompileOutput {
  std::vector<std::uint32_t> spirv;
};

class ShaderCompiler {
public:
  static ShaderCompileOutput CompileFile(const ShaderCompileInput &input);

private:
  static std::string ReadTextFile(const std::string &path);
};
} // namespace Velos
