#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float vortexSize;
uniform float vortexSpeed;
uniform float twist;
uniform vec3 swirlColor;
uniform float swirlGlow;
float pv_vortexSize() { return vortexSize; }
float pv_vortexSpeed() { return vortexSpeed; }
float pv_twist() { return twist; }
vec3 pv_swirlColor() { return swirlColor; }
float pv_swirlGlow() { return swirlGlow; }

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec2 n_n3_uv(vec2 uv) {
    return uv;
}

vec2 n_n4_p(vec2 uv) {
    return n_n3_uv(uv) - vec2(0.5, 0.5);
}

float n_n5_length(vec2 uv) {
    return length(n_n4_p(uv));
}

float n_n6_value(vec2 uv) {
    return n_n5_length(uv) / (abs(pv_vortexSize()) < 1e-6 ? 1e-6 : pv_vortexSize());
}

float n_n7_value(vec2 uv) {
    return n_n6_value(uv) * n_n6_value(uv);
}

float n_n8_value(vec2 uv) {
    return -n_n7_value(uv);
}

float n_n9_value(vec2 uv) {
    return exp(min(n_n8_value(uv), 80.0));
}

float n_n10_value(vec2 uv) {
    float x = u_time * pv_vortexSpeed() + 0.0;
    return mix(-1.0, 1.0, 0.5 - 0.5 * cos(x * 6.2832));
}

float n_n11_value(vec2 uv) {
    return n_n10_value(uv) * pv_twist();
}

float n_n12_value(vec2 uv) {
    return n_n9_value(uv) * n_n11_value(uv);
}

vec2 n_n13_p(vec2 uv) {
    return rot2(radians(n_n12_value(uv))) * n_n4_p(uv);
}

vec2 n_n14_p(vec2 uv) {
    return n_n13_p(uv) + vec2(0.5, 0.5);
}

vec4 n_n15_image(vec2 uv) {
    vec2 p = n_n14_p(uv);
    return n_n1_image(1.0 - abs(mod(p, 2.0) - 1.0));
}

float n_n16_curl(vec2 uv) {
    vec2 h = max(2.0, 0.01) / u_resolution;
    vec2 jx = (n_n14_p((uv + vec2(h.x, 0.0))) - n_n14_p((uv + vec2(-h.x, 0.0)))) / (2.0 * h.x);
    vec2 jy = (n_n14_p((uv + vec2(0.0, h.y))) - n_n14_p((uv + vec2(0.0, -h.y)))) / (2.0 * h.y);
    return jx.y - jy.x;
}

float n_n17_value(vec2 uv) {
    return abs(n_n16_curl(uv));
}

float n_n18_value(vec2 uv) {
    float t = clamp((n_n17_value(uv) - 0.0) / (4.0 - 0.0 + 1e-6), 0.0, 1.0);
    return mix(0.0, 1.0, t);
}

vec4 n_n19_image(vec2 uv) {
    return mix(vec4(0.0, 0.0, 0.0, 1.0), vec4(pv_swirlColor(), 1.0), clamp(n_n18_value(uv), 0.0, 1.0));
}

vec4 n_n20_image(vec2 uv) {
    vec4 b = n_n15_image(uv);
    vec4 l = n_n19_image(uv);
    vec3 rgb = 1.0 - (1.0 - b.rgb) * (1.0 - l.rgb);
    return vec4(mix(b.rgb, rgb, clamp(pv_swirlGlow() * l.a, 0.0, 1.0)), b.a);
}

float n_n2_mask(vec2 uv) {
    return driftMask(uv);
}

vec4 n_n21_image(vec2 uv) {
    return mix(n_n20_image(uv), n_n1_image(uv), clamp(n_n2_mask(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n21_image(v_texCoord);
}
