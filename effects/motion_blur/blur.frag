#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform sampler2D u_texture2;
uniform sampler2D u_texture3;
uniform vec2 u_resolution;
uniform float shutter;
uniform float samples;
uniform float maxBlur;
uniform float strength;

vec2 flowTexel(sampler2D s, ivec2 p, ivec2 sz) {
    vec4 b = texelFetch(s, clamp(p, ivec2(0), sz - 1), 0);
    return unpackFlow(b);
}

vec2 flowAt(sampler2D s, vec2 uv) {
    ivec2 sz = textureSize(s, 0);
    vec2 f = uv * vec2(sz) - 0.5;
    ivec2 i = ivec2(floor(f));
    vec2 t = f - vec2(i);
    return mix(mix(flowTexel(s, i, sz), flowTexel(s, i + ivec2(1, 0), sz), t.x),
               mix(flowTexel(s, i + ivec2(0, 1), sz), flowTexel(s, i + ivec2(1, 1), sz), t.x), t.y);
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

float luma(vec4 c) {
    return dot(c.rgb, vec3(0.299, 0.587, 0.114));
}

// How badly the frame before (or after) this pixel's neighbourhood, moved by `flow`, matches the current
// one: a few taps around the pixel.
float matchError(vec2 flow, bool before) {
    float e = 0.0;
    for (int i = 0; i < 5; ++i) {
        vec2 d = i == 0 ? vec2(0.0)
                        : vec2((i & 1) == 0 ? -1.0 : 1.0, (i & 2) == 0 ? -1.0 : 1.0) * 2.0 / u_resolution;
        vec2 uv = v_texCoord + d;
        vec4 other = before ? historyFrame(1.0, uv + flow) : futureFrame(1.0, uv + flow);
        e += abs(luma(texture(u_currentTexture, uv)) - luma(other));
    }
    return e;
}

// After McGuire et al., "A Reconstruction Filter for Plausible Motion Blur": taps run along the longest
// velocity near the pixel, and each counts by whether its own velocity reaches back to the pixel, so a
// fast object smears past its outline onto what is behind it. There is no depth here, so a tap that
// covers the pixel and a pixel that covers the tap weigh the same. A tap sampled at time s (-0.5 to 0.5
// of the shutter) reaches the pixel when its velocity, over the 2|s| of the shutter it has had, covers
// the distance: a tap on the earlier side by its backward flow, one on the later side by its forward flow.
//
// The shutter is centred on the frame. Its earlier half follows the flow towards the previous frame and
// its later half the flow towards the next one, so accelerating or curving motion bends the smear. Where
// the two disagree beyond what acceleration explains (an occlusion), the one that matches the footage
// better stands in for both. With no frame on one side the other side's flow is mirrored.
void main() {
    vec4 src = texture(u_currentTexture, v_texCoord);
    bool hasBefore = historyCount() > 0.5;
    bool hasAfter = futureCount() > 0.5;
    if (!hasBefore && !hasAfter) {
        fragColor = src;
        return;
    }
    ivec2 nsz = textureSize(u_texture2, 0);
    vec4 nb = texelFetch(u_texture2, clamp(ivec2(v_texCoord * vec2(nsz)), ivec2(0), nsz - 1), 0);
    vec2 vn = toPixels(unpackFlow(nb));
    float len = length(vn);
    if (len < 0.75) {
        fragColor = src;
        return;
    }

    vec2 back = flowAt(u_texture1, v_texCoord);
    vec2 ahead = flowAt(u_texture3, v_texCoord);
    vec2 pb = toPixels(back);
    vec2 pf = toPixels(ahead);
    if (!hasBefore) {
        pb = -pf;
    } else if (!hasAfter) {
        pf = -pb;
    } else {
        vec2 rb = back * u_resolution;
        vec2 rf = ahead * u_resolution;
        if (length(rb + rf) > 0.6 * max(length(rb), length(rf)) + 1.0) {
            if (matchError(back, true) <= matchError(ahead, false))
                pf = -pb;
            else
                pb = -pf;
        }
    }
    float ownBefore = length(pb);
    float ownAfter = length(pf);
    float speedX = max(ownBefore, ownAfter);
    if (speedX < 0.8 * len) {
        pb = vn;
        pf = -vn;
    }
    int n = clamp(int(ceil(max(length(pb), length(pf)))), 2, max(int(samples + 0.5), 2));
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

    float centreWeight = float(n) / (40.0 * max(speedX, 0.5));
    vec4 acc = vec4(src.rgb * src.a, src.a) * centreWeight;
    float total = centreWeight;
    for (int i = 0; i < n; ++i) {
        float s = (float(i) + jitter) / float(n) - 0.5;
        vec2 off = s < 0.0 ? pb * s : -pf * s;
        vec2 uv = v_texCoord + off / u_resolution;
        float dist = length(off);
        bool useBack = s < 0.0 ? hasBefore : !hasAfter;
        float speedY = length(toPixels(useBack ? flowAt(u_texture1, uv) : flowAt(u_texture3, uv)));
        float own = s < 0.0 ? ownBefore : ownAfter;
        speedY *= 2.0 * abs(s);
        float w = 0.5 * cone(dist, speedY) + 0.5 * cone(dist, own)
                  + 2.0 * cylinder(dist, speedY) * cylinder(dist, own);
        vec4 c = texture(u_currentTexture, uv);
        acc += vec4(c.rgb * c.a, c.a) * w;
        total += w;
    }
    acc /= total;
    fragColor = vec4(acc.a > 1e-5 ? acc.rgb / acc.a : vec3(0.0), acc.a);
}
