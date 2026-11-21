#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;

// The longest motion of the 4x4 smoothed-flow texels this texel of the sixteenth-size buffer covers,
// counting both directions: the backward flow as it is, and the forward flow reversed, so the result
// always reads like a backward flow.
void main() {
    ivec2 sz = textureSize(u_currentTexture, 0);
    ivec2 origin = ivec2(floor(v_texCoord * vec2(sz))) - 2;
    vec2 best = vec2(0.0);
    float bestLen = -1.0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            ivec2 p = clamp(origin + ivec2(x, y), ivec2(0), sz - 1);
            vec2 back = unpackFlow(texelFetch(u_currentTexture, p, 0));
            vec2 ahead = -unpackFlow(texelFetch(u_texture1, p, 0));
            float backLen = length(back * vec2(sz));
            float aheadLen = length(ahead * vec2(sz));
            if (backLen > bestLen) {
                bestLen = backLen;
                best = back;
            }
            if (aheadLen > bestLen) {
                bestLen = aheadLen;
                best = ahead;
            }
        }
    fragColor = packFlow(best);
}
