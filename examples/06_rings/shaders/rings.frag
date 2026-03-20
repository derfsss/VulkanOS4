#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
    /* Concentric rings using length, step, smoothstep, fract, mix */
    float dist = length(uv);
    float rings = fract(dist * 6.0);

    /* Sharp rings with smoothstep edges */
    float band = smoothstep(0.0, 0.05, rings) * (1.0 - smoothstep(0.45, 0.5, rings));

    /* Color by distance from center */
    vec3 innerColor = vec3(1.0, 0.4, 0.1);   /* orange */
    vec3 outerColor = vec3(0.1, 0.3, 1.0);   /* blue */
    vec3 ringColor  = mix(innerColor, outerColor, clamp(dist, 0.0, 1.0));
    vec3 bgColor    = vec3(0.05, 0.05, 0.1);  /* dark blue */

    vec3 color = mix(bgColor, ringColor, band);
    outColor = vec4(color, 1.0);
}
