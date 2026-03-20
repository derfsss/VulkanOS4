#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float time;
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 0) out vec2 fragUV;

void main() {
    gl_Position = vec4(inPos, 0.999, 1.0);
    fragUV = inPos * 0.5 + 0.5;
}
