#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float shutter;
uniform float samples;
uniform float maxBlur;

float dec(vec2 b) {
    return (floor(b.x * 255.0 + 0.5) * 256.0 + floor(b.y * 255.0 + 0.5)) / 65535.0 - 0.5;
}

vec2 flowTexel(ivec2 p, ivec2 sz) {
    vec4 b = texture(u_texture1, (vec2(clamp(p, ivec2(0), sz - 1)) + 0.5) / vec2(sz));
    return vec2(dec(b.rg), dec(b.ba));
}

vec2 flowAt(vec2 uv) {
    ivec2 sz = textureSize(u_texture1, 0);
    vec2 f = uv * vec2(sz) - 0.5;
    ivec2 i = ivec2(floor(f));
    vec2 t = f - vec2(i);
    return mix(mix(flowTexel(i, sz), flowTexel(i + ivec2(1, 0), sz), t.x),
               mix(flowTexel(i + ivec2(0, 1), sz), flowTexel(i + ivec2(1, 1), sz), t.x), t.y);
}

void main() {
    vec4 src = texture(u_currentTexture, v_texCoord);
    if (historyCount() < 0.5) {
        fragColor = src;
        return;
    }
    vec2 px = flowAt(v_texCoord) * clamp(shutter, 0.0, 360.0) / 360.0 * u_resolution;
    float len = length(px);
    float limit = maxBlur * 0.01 * u_resolution.x;
    if (len > limit) {
        px *= limit / len;
        len = limit;
    }
    if (len < 0.75) {
        fragColor = src;
        return;
    }
    int n = clamp(int(ceil(len)), 2, max(int(samples + 0.5), 2));
    vec2 step = px / u_resolution;
    vec4 acc = vec4(0.0);
    for (int i = 0; i < n; ++i) {
        vec4 c = texture(u_currentTexture, v_texCoord + step * ((float(i) + 0.5) / float(n) - 0.5));
        acc += vec4(c.rgb * c.a, c.a);
    }
    acc /= float(n);
    fragColor = vec4(acc.a > 1e-5 ? acc.rgb / acc.a : vec3(0.0), acc.a);
}
