#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

void main() {
    /* Assign colors based on dominant normal axis using comparisons
    ** and select (ternary) -- exercises OpSelect + comparisons.
    **
    ** +X/-X faces: red/dark red
    ** +Y/-Y faces: green/dark green
    ** +Z/-Z faces: blue/dark blue
    */
    vec3 n = normalize(fragNormal);
    vec3 an = abs(n);

    /* Find dominant axis using comparisons */
    float r = (an.x > an.y && an.x > an.z) ? (n.x > 0.0 ? 1.0 : 0.4) : 0.1;
    float g = (an.y > an.x && an.y > an.z) ? (n.y > 0.0 ? 1.0 : 0.4) : 0.1;
    float b = (an.z > an.x && an.z > an.y) ? (n.z > 0.0 ? 1.0 : 0.4) : 0.1;

    /* Simple directional lighting: dot(normal, lightDir) */
    vec3 lightDir = normalize(vec3(0.5, 0.7, 1.0));
    float diffuse = max(dot(n, lightDir), 0.2);

    outColor = vec4(vec3(r, g, b) * diffuse, 1.0);
}
