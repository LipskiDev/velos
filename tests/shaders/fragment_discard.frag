#version 450

layout(location = 0) out vec4 outputColor;

void main() {
  if (gl_FragCoord.x >= 4.0)
    discard;
  outputColor = vec4(1.0, 0.125490196, 0.501960784, 1.0);
}
