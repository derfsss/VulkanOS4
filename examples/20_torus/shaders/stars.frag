#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float time;
} pc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    float t = pc.time * 0.4;

    /* Deep space background gradient */
    float vy = fragUV.y;
    vec3 bg = vec3(0.01, 0.01, 0.04) + vec3(0.02, 0.0, 0.03) * vy;

    /* Animated ripple waves across the background */
    float cx = fragUV.x - 0.5;
    float cy = fragUV.y - 0.5;
    float dist = cx * cx + cy * cy;
    float ripple = sin(dist * 30.0 - t * 3.0) * 0.5 + 0.5;
    ripple = ripple * ripple * ripple;

    /* Star-like bright dots using overlapping sine grids.
    ** Where two sine waves both peak near +1, a bright dot appears.
    ** Uses only sin() -- no floor/fract/hash needed. */
    float s1 = sin(fragUV.x * 40.0) * sin(fragUV.y * 40.0);
    float s2 = sin(fragUV.x * 63.0 + 1.5) * sin(fragUV.y * 57.0 + 2.3);
    float s3 = sin(fragUV.x * 91.0 + 4.1) * sin(fragUV.y * 83.0 + 3.7);

    float star1 = max(s1 - 0.92, 0.0) * 12.5;
    float star2 = max(s2 - 0.93, 0.0) * 14.3;
    float star3 = max(s3 - 0.94, 0.0) * 16.7;

    /* Twinkle */
    float tw1 = sin(t * 2.0 + star1 * 50.0) * 0.3 + 0.7;
    float tw2 = sin(t * 3.1 + star2 * 70.0) * 0.3 + 0.7;
    float tw3 = sin(t * 1.7 + star3 * 30.0) * 0.3 + 0.7;

    float stars = star1 * tw1 + star2 * tw2 + star3 * tw3;

    /* Combine: dark background + white stars + subtle blue ripple */
    vec3 col = bg + vec3(0.9, 0.9, 1.0) * stars
             + vec3(0.02, 0.03, 0.08) * ripple;

    outColor = vec4(col, 1.0);
}
