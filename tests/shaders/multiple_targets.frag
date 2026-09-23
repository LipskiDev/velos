#version 460
layout(location = 0) out vec4 firstTarget;
layout(location = 1) out vec4 secondTarget;
void main() {
    firstTarget = vec4(1, 0, 0, 1);
    secondTarget = vec4(0, 1, 0, 1);
}
