#version 450

layout(push_constant) uniform PushConstants {
    float angle;
} pc;

layout(location = 0) out vec3 fragColor;

vec2 positions[3] = vec2[](
    vec2( 0.0, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5)
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

void main() {
    vec2 pos = positions[gl_VertexIndex];

    /* 2D rotation using push constant angle */
    float c = cos(pc.angle);
    float s = sin(pc.angle);
    vec2 rotated = vec2(
        pos.x * c - pos.y * s,
        pos.x * s + pos.y * c
    );

    gl_Position = vec4(rotated, 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
