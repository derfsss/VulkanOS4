#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 0.7, 1.0));
    float diffuse = max(dot(normalize(fragNormal), lightDir), 0.2);
    outColor = vec4(fragColor * diffuse, 1.0);
}
