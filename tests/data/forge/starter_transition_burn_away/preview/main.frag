#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float patchSize;
uniform vec3 fireColor;
float pv_patchSize() { return patchSize; }
vec3 pv_fireColor() { return fireColor; }
uniform float k_n3_seed;
uniform float k_n3_softness;
uniform float k_n5_sharpness;
uniform float k_n5_intensity;
uniform float k_n6_opacity;

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += amp * valueNoise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return v;
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return texture(u_texture1, uv);
}

float n_n3_mask(vec2 uv) {
    float x = clamp((fbm(uv * vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0) * pv_patchSize() + k_n3_seed * 7.31) - 0.2) / 0.6, 0.0, 1.0);
    float s = max(k_n3_softness, 0.0001);
    float front = mix(-s, 1.0 + s, u_progress);
    return 1.0 - smoothstep(front - s, front + s, x);
}

vec4 n_n4_image(vec2 uv) {
    return mix(n_n1_image(uv), n_n2_image(uv), clamp(n_n3_mask(uv), 0.0, 1.0));
}

vec4 n_n5_image(vec2 uv) {
    float m = clamp(n_n3_mask(uv), 0.0, 1.0);
    float band = pow(1.0 - abs(m * 2.0 - 1.0), k_n5_sharpness);
    return vec4(vec4(pv_fireColor(), 1.0).rgb * band * k_n5_intensity, band);
}

vec4 n_n6_image(vec2 uv) {
    vec4 b = n_n4_image(uv);
    vec4 l = n_n5_image(uv);
    vec3 rgb = min(b.rgb + l.rgb, 1.0);
    return vec4(mix(b.rgb, rgb, clamp(k_n6_opacity * l.a, 0.0, 1.0)), b.a);
}

void main() {
    fragColor = n_n6_image(v_texCoord);
}
