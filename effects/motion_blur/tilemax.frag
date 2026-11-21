#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;

float dec(vec2 b) {
    return (floor(b.x * 255.0 + 0.5) * 256.0 + floor(b.y * 255.0 + 0.5)) / 65535.0 - 0.5;
}

// The longest flow vector of the 4x4 smoothed-flow texels this texel of the sixteenth-size buffer covers.
void main() {
    ivec2 sz = textureSize(u_currentTexture, 0);
    ivec2 origin = ivec2(floor(v_texCoord * vec2(sz))) - 2;
    vec4 best = vec4(0.0);
    float bestLen = -1.0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            vec4 b = texelFetch(u_currentTexture, clamp(origin + ivec2(x, y), ivec2(0), sz - 1), 0);
            float len = length(vec2(dec(b.rg), dec(b.ba)) * vec2(sz));
            if (len > bestLen) {
                bestLen = len;
                best = b;
            }
        }
    fragColor = best;
}
