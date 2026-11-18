#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float u_time;
uniform float rings;
uniform float ringSpeed;
uniform float refraction;
uniform vec3 auraColor;
uniform float auraGlow;
float pv_rings() { return rings; }
float pv_ringSpeed() { return ringSpeed; }
float pv_refraction() { return refraction; }
vec3 pv_auraColor() { return auraColor; }
float pv_auraGlow() { return auraGlow; }

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec2 n_n19_uv(vec2 uv) {
    return uv;
}

vec4 n_n3_image(vec2 uv) {
    return texture(u_texture1, uv);
}

float n_n4_r(vec2 uv) {
    return n_n3_image(uv).r;
}

vec2 n_n16_grad(vec2 uv) {
    vec2 h = max(2.0, 0.01) / u_resolution;
    vec2 g = vec2(n_n4_r(uv + vec2(h.x, 0.0)) - n_n4_r(uv + vec2(-h.x, 0.0)), n_n4_r(uv + vec2(0.0, h.y)) - n_n4_r(uv + vec2(0.0, -h.y))) / (2.0 * h);
    return g;
}

float n_n5_value(vec2 uv) {
    return n_n4_r(uv) * pv_rings();
}

float n_n6_value(vec2 uv) {
    return u_time * pv_ringSpeed();
}

float n_n7_value(vec2 uv) {
    return n_n5_value(uv) + n_n6_value(uv);
}

float n_n8_value(vec2 uv) {
    return fract(n_n7_value(uv));
}

float n_n9_value(vec2 uv) {
    return n_n8_value(uv) - 0.5;
}

float n_n10_value(vec2 uv) {
    return abs(n_n9_value(uv));
}

float n_n11_value(vec2 uv) {
    return smoothstep(0.36, max(0.5, 0.36 + 1e-5), n_n10_value(uv));
}

float n_n12_value(vec2 uv) {
    return smoothstep(0.02, max(0.3, 0.02 + 1e-5), n_n4_r(uv));
}

float n_n2_mask(vec2 uv) {
    return driftMask(uv);
}

float n_n13_value(vec2 uv) {
    return 1.0 - n_n2_mask(uv);
}

float n_n14_value(vec2 uv) {
    return n_n12_value(uv) * n_n13_value(uv);
}

float n_n15_value(vec2 uv) {
    return n_n11_value(uv) * n_n14_value(uv);
}

float n_n17_value(vec2 uv) {
    return n_n15_value(uv) * pv_refraction();
}

vec2 n_n18_p(vec2 uv) {
    return n_n16_grad(uv) * n_n17_value(uv);
}

vec2 n_n20_p(vec2 uv) {
    return n_n19_uv(uv) + n_n18_p(uv);
}

vec4 n_n21_image(vec2 uv) {
    vec2 p = n_n20_p(uv);
    return n_n1_image(clamp(p, 0.0, 1.0));
}

vec4 n_n22_image(vec2 uv) {
    return mix(vec4(0.0, 0.0, 0.0, 1.0), vec4(pv_auraColor(), 1.0), clamp(n_n15_value(uv), 0.0, 1.0));
}

vec4 n_n23_image(vec2 uv) {
    vec4 b = n_n21_image(uv);
    vec4 l = n_n22_image(uv);
    vec3 rgb = 1.0 - (1.0 - b.rgb) * (1.0 - l.rgb);
    return vec4(mix(b.rgb, rgb, clamp(pv_auraGlow() * l.a, 0.0, 1.0)), b.a);
}

vec4 n_n24_image(vec2 uv) {
    return mix(n_n23_image(uv), n_n1_image(uv), clamp(n_n2_mask(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n24_image(v_texCoord);
}
