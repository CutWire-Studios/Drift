#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float u_time;
uniform float lineSoftness;
uniform float colorSpin;
uniform vec3 neonColor;
uniform float neonIntensity;
uniform float backdrop;
uniform float pulseRate;
float pv_lineSoftness() { return lineSoftness; }
float pv_colorSpin() { return colorSpin; }
vec3 pv_neonColor() { return neonColor; }
float pv_neonIntensity() { return neonIntensity; }
float pv_backdrop() { return backdrop; }
float pv_pulseRate() { return pulseRate; }

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

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n16_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 rgb = (col.rgb - 0.5) * 1.0 + 0.5 + pv_backdrop();
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}

float n_n2_mask(vec2 uv) {
    return driftMask(uv);
}

vec4 n_n17_image(vec2 uv) {
    return mix(n_n16_image(uv), n_n1_image(uv), clamp(n_n2_mask(uv), 0.0, 1.0));
}

vec4 n_n12_color(vec2 uv) {
    return vec4(pv_neonColor(), 1.0);
}

vec2 n_n3_grad(vec2 uv) {
    vec2 h = max(2.0, 0.01) / u_resolution;
    vec2 g = vec2(n_n2_mask(uv + vec2(h.x, 0.0)) - n_n2_mask(uv + vec2(-h.x, 0.0)), n_n2_mask(uv + vec2(0.0, h.y)) - n_n2_mask(uv + vec2(0.0, -h.y))) / (2.0 * h);
    return g;
}

float n_n8_angle(vec2 uv) {
    vec2 a = n_n3_grad(uv);
    return degrees(atan(a.y, a.x));
}

float n_n9_value(vec2 uv) {
    return u_time * 1.0;
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
    vec2 h = max(2.0, 0.01) / u_resolution;
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
    return smoothstep(0.02, max(pv_lineSoftness(), 0.02 + 1e-5), n_n6_value(uv));
}

vec4 n_n14_image(vec2 uv) {
    return mix(vec4(0.0, 0.0, 0.0, 1.0), n_n13_image(uv), clamp(n_n7_value(uv), 0.0, 1.0));
}

vec4 n_n15_image(vec2 uv) {
    vec4 col = n_n14_image(uv);
    vec3 g = texture(u_texture1, uv).rgb * vec4(1.0, 1.0, 1.0, 1.0).rgb * pv_neonIntensity();
    return vec4(clamp(col.rgb + g, 0.0, 1.0), col.a);
}

float n_n18_value(vec2 uv) {
    float x = u_time * pv_pulseRate() + 0.0;
    return mix(0.55, 1.0, 0.5 - 0.5 * cos(x * 6.2832));
}

vec4 n_n19_image(vec2 uv) {
    vec4 b = n_n17_image(uv);
    vec4 l = n_n15_image(uv);
    vec3 rgb = min(b.rgb + l.rgb, 1.0);
    return vec4(mix(b.rgb, rgb, clamp(n_n18_value(uv) * l.a, 0.0, 1.0)), b.a);
}

void main() {
    fragColor = n_n19_image(v_texCoord);
}
