#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform sampler2D u_texture2;
uniform vec2 u_resolution;
uniform float shutter;
uniform float samples;
uniform float maxBlur;
uniform float strength;

float dec(vec2 b) {
    return (floor(b.x * 255.0 + 0.5) * 256.0 + floor(b.y * 255.0 + 0.5)) / 65535.0 - 0.5;
}

vec2 flowTexel(ivec2 p, ivec2 sz) {
    vec4 b = texelFetch(u_texture1, clamp(p, ivec2(0), sz - 1), 0);
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

vec2 toPixels(vec2 flow) {
    vec2 px = flow * (clamp(shutter, 0.0, 360.0) / 360.0 * strength) * u_resolution;
    float len = length(px);
    float limit = maxBlur * 0.01 * u_resolution.x;
    return len > limit ? px * (limit / len) : px;
}

float cone(float dist, float speed) {
    return speed < 0.5 ? 0.0 : clamp(1.0 - dist / speed, 0.0, 1.0);
}

float cylinder(float dist, float speed) {
    return speed < 0.5 ? 0.0 : 1.0 - smoothstep(0.95 * speed, 1.05 * speed, dist);
}

// After McGuire et al., "A Reconstruction Filter for Plausible Motion Blur": taps run along the longest
// velocity near the pixel, and each counts by whether its own velocity reaches back to the pixel, so a
// fast object smears past its outline onto what is behind it. There is no depth here, so a tap that
// covers the pixel and a pixel that covers the tap weigh the same.
void main() {
    vec4 src = texture(u_currentTexture, v_texCoord);
    if (historyCount() < 0.5) {
        fragColor = src;
        return;
    }
    ivec2 nsz = textureSize(u_texture2, 0);
    vec4 nb = texelFetch(u_texture2, clamp(ivec2(v_texCoord * vec2(nsz)), ivec2(0), nsz - 1), 0);
    vec2 vn = toPixels(vec2(dec(nb.rg), dec(nb.ba)));
    float len = length(vn);
    if (len < 0.75) {
        fragColor = src;
        return;
    }
    vec2 vx = toPixels(flowAt(v_texCoord));
    float speedX = length(vx);
    int n = clamp(int(ceil(len)), 2, max(int(samples + 0.5), 2));
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

    float centreWeight = float(n) / (40.0 * max(speedX, 0.5));
    vec4 acc = vec4(src.rgb * src.a, src.a) * centreWeight;
    float total = centreWeight;
    for (int i = 0; i < n; ++i) {
        vec2 off = vn * ((float(i) + jitter) / float(n) - 0.5);
        vec2 uv = v_texCoord + off / u_resolution;
        float dist = length(off);
        float speedY = length(toPixels(flowAt(uv)));
        float w = 0.5 * cone(dist, speedY) + 0.5 * cone(dist, speedX)
                  + 2.0 * cylinder(dist, speedY) * cylinder(dist, speedX);
        vec4 c = texture(u_currentTexture, uv);
        acc += vec4(c.rgb * c.a, c.a) * w;
        total += w;
    }
    acc /= total;
    fragColor = vec4(acc.a > 1e-5 ? acc.rgb / acc.a : vec3(0.0), acc.a);
}
