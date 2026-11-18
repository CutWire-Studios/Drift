#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float growth;
uniform vec2 portalCentre;
uniform vec3 edgeColor;
float pv_growth() { return growth; }
vec2 pv_portalCentre() { return portalCentre; }
vec3 pv_edgeColor() { return edgeColor; }

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

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
    mat2 M = mat2(n_n7_value(uv), 0.0, 0.0, n_n7_value(uv));
    float det = determinant(M);
    M = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 o = pv_portalCentre();
    vec2 q = (uv - o) * vec2(asp, 1.0);
    return (M * q) / vec2(asp, 1.0) + o + vec2(0.0, 0.0);
}

vec4 n_n9_image(vec2 uv) {
    vec2 p = n_n8_p(uv);
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return vec4(vec3(n_n3_mask(p)), 1.0);
}

float n_n10_r(vec2 uv) {
    return n_n9_image(uv).r;
}

float n_n11_slope(vec2 uv) {
    vec2 h = max(2.0, 0.01) / u_resolution;
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
    return smoothstep(0.02, max(0.2, 0.02 + 1e-5), n_n14_value(uv));
}

vec4 n_n16_image(vec2 uv) {
    return mix(vec4(0.0, 0.0, 0.0, 1.0), vec4(pv_edgeColor(), 1.0), clamp(n_n15_value(uv), 0.0, 1.0));
}

vec4 s_n17_0(vec2 uv) {
    vec4 col = n_n16_image(uv);
    return vec4(max(col.rgb - vec3(0.0), 0.0) / max(1.0 - 0.0, 0.0001), 1.0);
}

void main() {
    fragColor = s_n17_0(v_texCoord);
}
