#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;

const int kRadius = 4;
// R is the shown frame's luma, G the next one's; kRef is matched against kOther.
const int kRef = 1;
const int kOther = 0;
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

// looks like cur(x). Zero unless some shift beats staying put by a clear margin.
void main() {
    ivec2 sz = textureSize(u_currentTexture, 0);
    ivec2 p = ivec2(v_texCoord * vec2(sz));
    float base = cost(p, ivec2(0), sz);
    float best = base;
    ivec2 bestD = ivec2(0);
    if (base > kMinBase) {
        for (int dy = -kRadius; dy <= kRadius; ++dy)
            for (int dx = -kRadius; dx <= kRadius; ++dx) {
                ivec2 d = ivec2(dx, dy);
                float c = cost(p, d, sz) + 0.0002 * length(vec2(d));
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
