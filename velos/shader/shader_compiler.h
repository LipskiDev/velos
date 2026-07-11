#pragma once

#include "rhi/types.h"
#include <spirv_reflect.h>
#include <string>
#include <vector>
namespace Velos {
enum class ShaderSourceLanguage {
  GLSL,
  HLSL,
  SpirvBinary,
  Slang,
};

enum class ShaderBinaryFormat {
  Spirv,
};

struct ShaderCompileInput {
  std::string path;
  Velos::RHI::ShaderStage stage = Velos::RHI::ShaderStage::None;
  std::string entryPoint = "main";

  ShaderSourceLanguage language = ShaderSourceLanguage::GLSL;
  ShaderBinaryFormat outputFormat = ShaderBinaryFormat::Spirv;
};

enum class ShaderResourceType {
  UniformBuffer,
  StorageBuffer,
  SampledImage,
  StorageImage,
  Sampler,
  CombinedImageSampler,
  InputAttachment,
  AccelerationStructure,
};

struct ShaderResourceBinding {
  std::string name;
  ShaderResourceType type;
  u32 set = 0;
  u32 binding = 0;
  u32 arraySize = 1;
  RHI::ShaderStage stage;
};

struct PushConstantRangeInfo {
  u32 offset = 0;
  u32 size = 0;
  RHI::ShaderStage stage;
};

struct ShaderReflectionData {
  std::string entryPoint;
  RHI::ShaderStage stage;

  std::vector<ShaderResourceBinding> resources;
  std::vector<PushConstantRangeInfo> pushConstants;
};

struct ShaderCompileOutput {
  std::vector<std::uint32_t> spirv;
  ShaderReflectionData reflection;
};

class ShaderCompiler {
public:
  static ShaderCompileOutput CompileFile(const ShaderCompileInput &input);

private:
  static ShaderCompileOutput
  CompileGlslToSpirv(const ShaderCompileInput &input);
  static ShaderCompileOutput
  CompileHlslToSpirv(const ShaderCompileInput &input);
  static ShaderCompileOutput
  CompileSlangToSpirv(const ShaderCompileInput &input);
  static ShaderCompileOutput LoadSpirvBinary(const std::string &path);

  static ShaderReflectionData ReflectSpirv(const std::vector<uint32_t> &spirv,
                                           RHI::ShaderStage stage);
  static void ReflectPushConstants(const SpvReflectShaderModule &module,
                                   RHI::ShaderStage stage,
                                   ShaderReflectionData &out);

private:
  static std::string ReadTextFile(const std::string &path);
};
} // namespace Velos
