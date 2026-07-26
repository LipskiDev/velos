#version 450

layout(location = 0) out vec4 outputColor;

void main() {
  uvec2 pixel = uvec2(gl_FragCoord.xy);
  outputColor = vec4(
      float(pixel.x * 29u + 11u) / 255.0,
      float(pixel.y * 37u + 7u) / 255.0,
      float((pixel.x ^ pixel.y) * 41u + 3u) / 255.0,
      1.0);
}
