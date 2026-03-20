#version 450

layout(push_constant) uniform PushConstants {
    float time;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
    /* Animated rainbow wave bands using sin/cos + time offset */
    float t = pc.time;
    float r = sin(uv.x * 12.0 + uv.y * 4.0 + t * 2.0) * 0.5 + 0.5;
    float g = sin(uv.x * 8.0 - uv.y * 6.0 + t * 3.0 + 2.0) * 0.5 + 0.5;
    float b = cos(uv.y * 10.0 + uv.x * 3.0 + t * 1.5) * 0.5 + 0.5;
    outColor = vec4(r, g, b, 1.0);
}
