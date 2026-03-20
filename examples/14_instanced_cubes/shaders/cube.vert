#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragColor;

void main() {
    /* Compute per-instance offset from gl_InstanceIndex.
    ** Arrange instances in a 3x3 grid with spacing 1.5 units. */
    int idx = gl_InstanceIndex;
    float gridX = float(idx % 3) * 1.5 - 1.5;
    float gridY = float(idx / 3) * 1.5 - 1.5;

    vec3 offset = vec3(gridX, gridY, 0.0);
    vec4 worldPos = vec4(inPosition * 0.35 + offset, 1.0);

    gl_Position = pc.viewProj * worldPos;
    fragNormal = inNormal;

    /* Per-instance color from index */
    float r = float((idx + 1) & 1);
    float g = float(((idx + 1) >> 1) & 1);
    float b = float(((idx + 1) >> 2) & 1);
    fragColor = vec3(r * 0.5 + 0.3, g * 0.5 + 0.3, b * 0.5 + 0.3);
}
