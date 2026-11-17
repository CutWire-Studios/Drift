#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float contours;
uniform float lineWidth;
uniform vec3 lineColor;
float pv_contours() { return contours; }
float pv_lineWidth() { return lineWidth; }
vec3 pv_lineColor() { return lineColor; }

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n15_color(vec2 uv) {
    return vec4(pv_lineColor(), 1.0);
}

vec4 n_n2_image(vec2 uv) {
    return texture(u_texture1, uv);
}

float n_n3_l(vec2 uv) {
    return luma(n_n2_image(uv));
}

float n_n4_value(vec2 uv) {
    return n_n3_l(uv) * pv_contours();
}

float n_n5_value(vec2 uv) {
    return fract(n_n4_value(uv));
}

float n_n6_value(vec2 uv) {
    return n_n5_value(uv) - 0.5;
}

float n_n7_value(vec2 uv) {
    return abs(n_n6_value(uv));
}

float n_n8_value(vec2 uv) {
    return 0.5 - n_n7_value(uv);
}

float n_n9_slope(vec2 uv) {
    vec2 h = max(1.0, 0.01) / u_resolution;
    vec2 g = vec2(n_n4_value(uv + vec2(h.x, 0.0)) - n_n4_value(uv + vec2(-h.x, 0.0)), n_n4_value(uv + vec2(0.0, h.y)) - n_n4_value(uv + vec2(0.0, -h.y))) / (2.0 * h);
    return length(g);
}

vec2 n_n10_size(vec2 uv) {
    return u_resolution;
}

float n_n11_y(vec2 uv) {
    return n_n10_size(uv).y;
}

float n_n12_value(vec2 uv) {
    return n_n9_slope(uv) / (abs(n_n11_y(uv)) < 1e-6 ? 1e-6 : n_n11_y(uv));
}

float n_n13_value(vec2 uv) {
    return n_n8_value(uv) / (abs(n_n12_value(uv)) < 1e-6 ? 1e-6 : n_n12_value(uv));
}

float n_n14_value(vec2 uv) {
    float t = clamp((n_n13_value(uv) - 0.4) / (pv_lineWidth() - 0.4 + 1e-6), 0.0, 1.0);
    return mix(1.0, 0.0, t);
}

vec4 n_n16_image(vec2 uv) {
    return mix(n_n1_image(uv), n_n15_color(uv), clamp(n_n14_value(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n16_image(v_texCoord);
}
