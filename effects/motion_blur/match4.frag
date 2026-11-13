#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;

const float kMinGain = 0.015;

vec2 enc(float v) {
    float e = floor(clamp(v + 0.5, 0.0, 1.0) * 65535.0 + 0.5);
    float hi = floor(e / 256.0);
    return vec2(hi, e - hi * 256.0) / 255.0;
}

float dec(vec2 b) {
    return (floor(b.x * 255.0 + 0.5) * 256.0 + floor(b.y * 255.0 + 0.5)) / 65535.0 - 0.5;
}

float luma(ivec2 p, ivec2 sz, int channel) {
    vec2 t = (vec2(clamp(p, ivec2(0), sz - 1)) + 0.5) / vec2(sz);
    return texture(u_currentTexture, t)[channel];
}

float cost(ivec2 p, ivec2 d, ivec2 sz) {
    float sum = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
            sum += abs(luma(p + ivec2(x, y), sz, 0) - luma(p + ivec2(x, y) + d, sz, 1));
    return sum / 25.0;
}

// Refines the coarse flow around its own guess, within two texels each way.
void main() {
    ivec2 sz = textureSize(u_currentTexture, 0);
    ivec2 cs = textureSize(u_texture1, 0);
    ivec2 p = ivec2(v_texCoord * vec2(sz));
    ivec2 cp = clamp(ivec2(v_texCoord * vec2(cs)), ivec2(0), cs - 1);
    vec4 coarse = texture(u_texture1, (vec2(cp) + 0.5) / vec2(cs));
    ivec2 guess = ivec2(floor(vec2(dec(coarse.rg), dec(coarse.ba)) * vec2(sz) + 0.5));

    float base = cost(p, ivec2(0), sz);
    float best = base;
    ivec2 bestD = ivec2(0);
    if (base > kMinGain) {
        best = cost(p, guess, sz);
        bestD = guess;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                ivec2 d = guess + ivec2(dx, dy);
                float c = cost(p, d, sz) + 0.002 * length(vec2(dx, dy));
                if (c < best) {
                    best = c;
                    bestD = d;
                }
            }
        if (best > base - kMinGain)
            bestD = ivec2(0);
    }
    vec2 uvD = vec2(bestD) / vec2(sz);
    fragColor = vec4(enc(uvD.x), enc(uvD.y));
}
