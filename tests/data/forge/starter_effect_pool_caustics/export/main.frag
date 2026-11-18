#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float waveSize;
uniform float waveSpeed;
uniform float refraction;
uniform vec3 lightColor;
uniform float caustics;
float pv_waveSize() { return waveSize; }
float pv_waveSpeed() { return waveSpeed; }
float pv_refraction() { return refraction; }
vec3 pv_lightColor() { return lightColor; }
float pv_caustics() { return caustics; }

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

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

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec2 n_n5_uv(vec2 uv) {
    return uv;
}

float n_n2_value(vec2 uv) {
    return valueNoise(uv * vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0) * pv_waveSize() + vec2(u_time * pv_waveSpeed(), u_time * pv_waveSpeed() * 0.7) + vec2(0.0 * 17.31, 0.0 * 7.77));
}

vec2 n_n3_grad(vec2 uv) {
    vec2 h = max(2.0, 0.01) / u_resolution;
    vec2 g = vec2(n_n2_value(uv + vec2(h.x, 0.0)) - n_n2_value(uv + vec2(-h.x, 0.0)), n_n2_value(uv + vec2(0.0, h.y)) - n_n2_value(uv + vec2(0.0, -h.y))) / (2.0 * h);
    return g;
}

vec2 n_n4_p(vec2 uv) {
    return n_n3_grad(uv) * pv_refraction();
}

vec2 n_n6_p(vec2 uv) {
    return n_n5_uv(uv) + n_n4_p(uv);
}

vec4 n_n12_image(vec2 uv) {
    vec2 p = n_n6_p(uv);
    return n_n1_image(clamp(p, 0.0, 1.0));
}

float n_n7_det(vec2 uv) {
    vec2 h = max(2.0, 0.01) / u_resolution;
    vec2 jx = (n_n6_p((uv + vec2(h.x, 0.0))) - n_n6_p((uv + vec2(-h.x, 0.0)))) / (2.0 * h.x);
    vec2 jy = (n_n6_p((uv + vec2(0.0, h.y))) - n_n6_p((uv + vec2(0.0, -h.y)))) / (2.0 * h.y);
    return jx.x * jy.y - jy.x * jx.y;
}

float n_n8_value(vec2 uv) {
    return 1.0 / (abs(n_n7_det(uv)) < 1e-6 ? 1e-6 : n_n7_det(uv));
}

float n_n9_value(vec2 uv) {
    return abs(n_n8_value(uv));
}

float n_n10_value(vec2 uv) {
    float t = clamp((n_n9_value(uv) - 1.0) / (3.0 - 1.0 + 1e-6), 0.0, 1.0);
    return mix(0.0, 1.0, t);
}

vec4 n_n11_image(vec2 uv) {
    vec4 col = vec4(vec3(n_n10_value(uv)), 1.0);
    vec3 tinted = luma(col) * vec4(pv_lightColor(), 1.0).rgb * 1.6;
    return vec4(mix(col.rgb, clamp(tinted, 0.0, 1.0), 1.0), col.a);
}

vec4 n_n13_image(vec2 uv) {
    vec4 b = n_n12_image(uv);
    vec4 l = n_n11_image(uv);
    vec3 rgb = 1.0 - (1.0 - b.rgb) * (1.0 - l.rgb);
    return vec4(mix(b.rgb, rgb, clamp(pv_caustics() * l.a, 0.0, 1.0)), b.a);
}

void main() {
    fragColor = n_n13_image(v_texCoord);
}
