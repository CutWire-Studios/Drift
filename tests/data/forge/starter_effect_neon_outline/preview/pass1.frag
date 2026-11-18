#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float lineSoftness;
uniform float colorSpin;
uniform vec3 neonColor;
float pv_lineSoftness() { return lineSoftness; }
float pv_colorSpin() { return colorSpin; }
vec3 pv_neonColor() { return neonColor; }
uniform vec4 k_n14_a;
uniform float k_n3_spacing;
uniform vec2 k_n8_b;
uniform float k_n9_speed;
uniform float k_n7_low;
uniform float k_n15_threshold;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

vec4 n_n12_color(vec2 uv) {
    return vec4(pv_neonColor(), 1.0);
}

float n_n2_mask(vec2 uv) {
    return driftMask(uv);
}

vec2 n_n3_grad(vec2 uv) {
    vec2 h = max(k_n3_spacing, 0.01) / u_resolution;
    vec2 g = vec2(n_n2_mask(uv + vec2(h.x, 0.0)) - n_n2_mask(uv + vec2(-h.x, 0.0)), n_n2_mask(uv + vec2(0.0, h.y)) - n_n2_mask(uv + vec2(0.0, -h.y))) / (2.0 * h);
    return g;
}

float n_n8_angle(vec2 uv) {
    vec2 a = n_n3_grad(uv);
    return degrees(atan(a.y, a.x));
}

float n_n9_value(vec2 uv) {
    return u_time * k_n9_speed;
}

float n_n10_value(vec2 uv) {
    return n_n9_value(uv) * pv_colorSpin();
}

float n_n11_value(vec2 uv) {
    return n_n8_angle(uv) + n_n10_value(uv);
}

vec4 n_n13_image(vec2 uv) {
    vec4 col = n_n12_color(uv);
    vec3 hsv = rgb2hsv(col.rgb);
    hsv.x = fract(hsv.x + n_n11_value(uv) / 360.0);
    return vec4(hsv2rgb(hsv), col.a);
}

float n_n3_slope(vec2 uv) {
    vec2 h = max(k_n3_spacing, 0.01) / u_resolution;
    vec2 g = vec2(n_n2_mask(uv + vec2(h.x, 0.0)) - n_n2_mask(uv + vec2(-h.x, 0.0)), n_n2_mask(uv + vec2(0.0, h.y)) - n_n2_mask(uv + vec2(0.0, -h.y))) / (2.0 * h);
    return length(g);
}

vec2 n_n4_size(vec2 uv) {
    return u_resolution;
}

float n_n5_y(vec2 uv) {
    return n_n4_size(uv).y;
}

float n_n6_value(vec2 uv) {
    return n_n3_slope(uv) / (abs(n_n5_y(uv)) < 1e-6 ? 1e-6 : n_n5_y(uv));
}

float n_n7_value(vec2 uv) {
    return smoothstep(k_n7_low, max(pv_lineSoftness(), k_n7_low + 1e-5), n_n6_value(uv));
}

vec4 n_n14_image(vec2 uv) {
    return mix(k_n14_a, n_n13_image(uv), clamp(n_n7_value(uv), 0.0, 1.0));
}

vec4 s_n15_0(vec2 uv) {
    vec4 col = n_n14_image(uv);
    return vec4(max(col.rgb - vec3(k_n15_threshold), 0.0) / max(1.0 - k_n15_threshold, 0.0001), 1.0);
}

void main() {
    fragColor = s_n15_0(v_texCoord);
}
