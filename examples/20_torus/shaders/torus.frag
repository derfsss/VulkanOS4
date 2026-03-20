#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float time;
} pc;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.4, 0.7, 1.0));
    float diffuse = max(dot(n, lightDir), 0.15);

    vec4 texColor = texture(tex, fragUV);
    outColor = vec4(texColor.rgb * diffuse, 1.0);
}
