#version 450

layout(location = 0) in vec2 fragCoord;
layout(location = 0) out vec4 outColor;

void main() {
    /* Checkerboard pattern using floor() and mod() */
    float tileSize = 8.0;
    float x = floor(fragCoord.x * tileSize);
    float y = floor(fragCoord.y * tileSize);
    float checker = mod(x + y, 2.0);

    if (checker < 1.0)
        outColor = vec4(0.9, 0.9, 0.9, 1.0);  /* white */
    else
        outColor = vec4(0.2, 0.2, 0.2, 1.0);  /* dark grey */
}
