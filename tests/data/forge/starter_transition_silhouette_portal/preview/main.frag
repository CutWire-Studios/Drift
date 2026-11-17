#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform sampler2D u_texture2;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float growth;
uniform vec2 portalCentre;
uniform vec3 edgeColor;
uniform float edgeGlow;
float pv_growth() { return growth; }
vec2 pv_portalCentre() { return portalCentre; }
vec3 pv_edgeColor() { return edgeColor; }
float pv_edgeGlow() { return edgeGlow; }
uniform float k_n8_m10;
uniform float k_n8_m01;
uniform vec2 k_n8_offset;
uniform float k_n18_low;
uniform float k_n18_high;
uniform vec4 k_n16_a;
uniform float k_n15_low;
uniform float k_n15_high;
uniform float k_n11_spacing;
uniform vec4 k_n17_tint;

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return texture(u_texture1, uv);
}

float n_n3_mask(vec2 uv) {
    return driftMask(uv);
}

float n_n4_value(vec2 uv) {
    return u_progress;
}

float n_n5_value(vec2 uv) {
    return n_n4_value(uv) * n_n4_value(uv);
}

float n_n6_value(vec2 uv) {
    return n_n5_value(uv) * pv_growth();
}

float n_n7_value(vec2 uv) {
    return exp(min(n_n6_value(uv), 80.0));
}

vec2 n_n8_p(vec2 uv) {
    mat2 M = mat2(n_n7_value(uv), k_n8_m10, k_n8_m01, n_n7_value(uv));
    float det = determinant(M);
    M = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 o = pv_portalCentre();
    vec2 q = (uv - o) * vec2(asp, 1.0);
    return (M * q) / vec2(asp, 1.0) + o + k_n8_offset;
}

vec4 n_n9_image(vec2 uv) {
    vec2 p = n_n8_p(uv);
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return vec4(vec3(n_n3_mask(p)), 1.0);
}

float n_n10_r(vec2 uv) {
    return n_n9_image(uv).r;
}

float n_n18_value(vec2 uv) {
    return smoothstep(k_n18_low, max(k_n18_high, k_n18_low + 1e-5), n_n4_value(uv));
}

float n_n19_value(vec2 uv) {
    return max(n_n10_r(uv), n_n18_value(uv));
}

vec4 n_n20_image(vec2 uv) {
    return mix(n_n1_image(uv), n_n2_image(uv), clamp(n_n19_value(uv), 0.0, 1.0));
}

float n_n11_slope(vec2 uv) {
    vec2 h = max(k_n11_spacing, 0.01) / u_resolution;
    vec2 g = vec2(n_n10_r(uv + vec2(h.x, 0.0)) - n_n10_r(uv + vec2(-h.x, 0.0)), n_n10_r(uv + vec2(0.0, h.y)) - n_n10_r(uv + vec2(0.0, -h.y))) / (2.0 * h);
    return length(g);
}

vec2 n_n12_size(vec2 uv) {
    return u_resolution;
}

float n_n13_y(vec2 uv) {
    return n_n12_size(uv).y;
}

float n_n14_value(vec2 uv) {
    return n_n11_slope(uv) / (abs(n_n13_y(uv)) < 1e-6 ? 1e-6 : n_n13_y(uv));
}

float n_n15_value(vec2 uv) {
    return smoothstep(k_n15_low, max(k_n15_high, k_n15_low + 1e-5), n_n14_value(uv));
}

vec4 n_n16_image(vec2 uv) {
    return mix(k_n16_a, vec4(pv_edgeColor(), 1.0), clamp(n_n15_value(uv), 0.0, 1.0));
}

vec4 n_n17_glow(vec2 uv) {
    return vec4(clamp(texture(u_texture2, uv).rgb * k_n17_tint.rgb * pv_edgeGlow(), 0.0, 1.0), 1.0);
}

float n_n21_value(vec2 uv) {
    return 1.0 - n_n18_value(uv);
}

vec4 n_n22_image(vec2 uv) {
    vec4 b = n_n20_image(uv);
    vec4 l = n_n17_glow(uv);
    vec3 rgb = min(b.rgb + l.rgb, 1.0);
    return vec4(mix(b.rgb, rgb, clamp(n_n21_value(uv) * l.a, 0.0, 1.0)), b.a);
}

void main() {
    fragColor = n_n22_image(v_texCoord);
}
