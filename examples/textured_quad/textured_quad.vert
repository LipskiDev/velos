#version 450

layout(push_constant) uniform PushConstants {
  mat4 u_MVP;
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 vUV;

void main() {
  gl_Position = pc.u_MVP * vec4(inPos, 1.0);
  vUV = inUV;
}
