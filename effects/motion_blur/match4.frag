#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;

// Channel of the luma buffer holding the frame matched against: G is the previous frame, B the next.
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
            sum += abs(luma(p + ivec2(x, y), sz, 0) - luma(p + ivec2(x, y) + d, sz, kOther));
    return sum / 25.0;
}

// Refines the coarse flow around its own guess, within two texels each way, then to a fraction of a
// texel from the cost curve either side of the best shift.
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
    vec2 sub = vec2(0.0);
    if (base > kMinBase) {
        best = cost(p, guess, sz);
        bestD = guess;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                ivec2 d = guess + ivec2(dx, dy);
                float c = cost(p, d, sz) + 0.0002 * length(vec2(dx, dy));
                if (c < best) {
                    best = c;
                    bestD = d;
                }
            }
        if (!(best < kRelative * base)) {
            bestD = ivec2(0);
        } else {
            float c0 = cost(p, bestD, sz);
            float xm = cost(p, bestD - ivec2(1, 0), sz);
            float xp = cost(p, bestD + ivec2(1, 0), sz);
            float ym = cost(p, bestD - ivec2(0, 1), sz);
            float yp = cost(p, bestD + ivec2(0, 1), sz);
            float dx = xm - 2.0 * c0 + xp;
            float dy = ym - 2.0 * c0 + yp;
            if (dx > 1e-6 && c0 <= xm && c0 <= xp)
                sub.x = clamp(0.5 * (xm - xp) / dx, -0.5, 0.5);
            if (dy > 1e-6 && c0 <= ym && c0 <= yp)
                sub.y = clamp(0.5 * (ym - yp) / dy, -0.5, 0.5);
        }
    }
    vec2 uvD = (vec2(bestD) + sub) / vec2(sz);
    fragColor = packFlow(uvD);
}
