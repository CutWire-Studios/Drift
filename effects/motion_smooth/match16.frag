#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;

// R is the shown frame's luma, G the next one's; kRef is matched against kOther.
const int kRef = 0;
const int kOther = 1;
const float kMinBase = 0.004;
const float kRelative = 0.85;

float luma(ivec2 p, ivec2 sz, int channel) {
    vec2 t = (vec2(clamp(p, ivec2(0), sz - 1)) + 0.5) / vec2(sz);
    return texture(u_currentTexture, t)[channel];
}

float cost(ivec2 p, ivec2 d, ivec2 sz) {
    float sum = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
            sum += abs(luma(p + ivec2(x, y), sz, kRef) - luma(p + ivec2(x, y) + d, sz, kOther));
    return sum / 25.0;
}

// Refines the coarsest flow around its own guess, within three texels each way.
void main() {
    ivec2 sz = textureSize(u_currentTexture, 0);
    ivec2 cs = textureSize(u_texture1, 0);
    ivec2 p = ivec2(v_texCoord * vec2(sz));
    ivec2 cp = clamp(ivec2(v_texCoord * vec2(cs)), ivec2(0), cs - 1);
    vec4 coarse = texture(u_texture1, (vec2(cp) + 0.5) / vec2(cs));
    ivec2 guess = ivec2(floor(unpackFlow(coarse) * vec2(sz) + 0.5));

    float base = cost(p, ivec2(0), sz);
    float best = base;
    ivec2 bestD = ivec2(0);
    if (base > kMinBase) {
        best = cost(p, guess, sz);
        bestD = guess;
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx) {
                ivec2 d = guess + ivec2(dx, dy);
                float c = cost(p, d, sz) + 0.0002 * length(vec2(dx, dy));
                if (c < best) {
                    best = c;
                    bestD = d;
                }
            }
        if (!(best < kRelative * base))
            bestD = ivec2(0);
    }
    vec2 uvD = vec2(bestD) / vec2(sz);
    fragColor = packFlow(uvD);
}
