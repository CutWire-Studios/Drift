#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;

// The longest tile flow of the 3x3 tiles around this one, so a pixel next to a fast object still
// knows the object will sweep over it.
void main() {
    ivec2 sz = textureSize(u_currentTexture, 0);
    ivec2 p = ivec2(v_texCoord * vec2(sz));
    vec4 best = vec4(0.0);
    float bestLen = -1.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec4 b = texelFetch(u_currentTexture, clamp(p + ivec2(x, y), ivec2(0), sz - 1), 0);
            float len = length(unpackFlow(b));
            if (len > bestLen) {
                bestLen = len;
                best = b;
            }
        }
    fragColor = best;
}
