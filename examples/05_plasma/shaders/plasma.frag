#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
    /* Classic plasma effect using multiple sin/cos/sqrt (OpExtInst) */
    float x = uv.x * 10.0;
    float y = uv.y * 10.0;

    float v1 = sin(x + y);
    float v2 = sin(sqrt(x * x + y * y) * 2.0);
    float v3 = cos(x * 0.7 - y * 1.3);
    float v4 = sin(sqrt((x - 5.0) * (x - 5.0) + (y - 5.0) * (y - 5.0)) * 1.5);

    float v = (v1 + v2 + v3 + v4) * 0.25;

    float r = sin(v * 3.14159) * 0.5 + 0.5;
    float g = sin(v * 3.14159 + 2.094) * 0.5 + 0.5;
    float b = sin(v * 3.14159 + 4.189) * 0.5 + 0.5;

    outColor = vec4(r, g, b, 1.0);
}
