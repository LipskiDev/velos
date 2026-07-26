#version 450

layout(set = 0, binding = 0) uniform ColorParameters {
  vec4 color;
} parameters;

layout(location = 0) out vec4 outputColor;

void main() {
  outputColor = parameters.color;
}
