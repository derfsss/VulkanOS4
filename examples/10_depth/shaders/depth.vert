#version 450

layout(push_constant) uniform PushConstants {
    float offsetZ;    /* z-offset for each triangle */
    float offsetX;    /* x-offset */
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(inPos.x + pc.offsetX, inPos.y, pc.offsetZ, 1.0);
    fragColor = inColor;
}
