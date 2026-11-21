#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;

const int kReach = 12;
const float kMoving = 0.25;

vec2 enc(float v) {
    float e = floor(clamp(v + 0.5, 0.0, 1.0) * 65535.0 + 0.5);
    float hi = floor(e / 256.0);
    return vec2(hi, e - hi * 256.0) / 255.0;
}

float dec(vec2 b) {
    return (floor(b.x * 255.0 + 0.5) * 256.0 + floor(b.y * 255.0 + 0.5)) / 65535.0 - 0.5;
}

vec2 flowAt(ivec2 p, ivec2 sz) {
    vec4 b = texture(u_currentTexture, (vec2(clamp(p, ivec2(0), sz - 1)) + 0.5) / vec2(sz));
    return vec2(dec(b.rg), dec(b.ba));
}

// A hole is a texel without measured motion. It inherits motion only when moving texels sit on two
// opposite sides of it within kReach, which fills the flat inside of a moving object but not the
// background next to it. Elsewhere, a 5x5 average that ignores neighbours moving differently keeps an
// object's edge on its own flow.
void main() {
    ivec2 sz = textureSize(u_currentTexture, 0);
    ivec2 p = ivec2(v_texCoord * vec2(sz));
    vec2 centre = flowAt(p, sz);

    if (length(centre * vec2(sz)) < kMoving) {
        vec2 hit[8];
        float dist[8];
        for (int k = 0; k < 8; ++k) {
            float a = float(k) * 0.7853981634;
            ivec2 dir = ivec2(int(round(cos(a))), int(round(sin(a))));
            hit[k] = vec2(0.0);
            dist[k] = 0.0;
            for (int s = 1; s <= kReach; ++s) {
                vec2 f = flowAt(p + dir * s, sz);
                if (length(f * vec2(sz)) >= kMoving) {
                    hit[k] = f;
                    dist[k] = float(s);
                    break;
                }
            }
        }
        bool enclosed = false;
        for (int k = 0; k < 4; ++k)
            if (dist[k] > 0.0 && dist[k + 4] > 0.0)
                enclosed = true;
        vec2 sum = vec2(0.0);
        float weight = 0.0;
        if (enclosed)
            for (int k = 0; k < 8; ++k)
                if (dist[k] > 0.0) {
                    sum += hit[k] / dist[k];
                    weight += 1.0 / dist[k];
                }
        vec2 flow = weight > 0.0 ? sum / weight : vec2(0.0);
        fragColor = vec4(enc(flow.x), enc(flow.y));
        return;
    }

    float sigma = 0.01 + 0.25 * length(centre);
    vec2 sum = vec2(0.0);
    float weight = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            vec2 f = flowAt(p + ivec2(x, y), sz);
            vec2 diff = (f - centre) * vec2(sz) / (sigma * float(max(sz.x, sz.y)));
            float w = exp(-0.15 * float(x * x + y * y)) * exp(-dot(diff, diff));
            sum += f * w;
            weight += w;
        }
    vec2 flow = sum / weight;
    fragColor = vec4(enc(flow.x), enc(flow.y));
}
