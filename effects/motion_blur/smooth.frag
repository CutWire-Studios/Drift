#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;

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

// A 5x5 average that ignores neighbours moving differently, so an object's edge keeps its own flow.
void main() {
    ivec2 sz = textureSize(u_currentTexture, 0);
    ivec2 p = ivec2(v_texCoord * vec2(sz));
    vec2 centre = flowAt(p, sz);
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
