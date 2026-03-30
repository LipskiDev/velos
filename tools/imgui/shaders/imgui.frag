#version 450

layout(location = 0) in vec2 outUV;
layout(location = 1) in vec4 outColor;

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

void main() {
    fragColor = outColor * texture(uTexture, outUV);
}
