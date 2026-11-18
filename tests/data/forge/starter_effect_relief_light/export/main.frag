#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float lightAngle;
uniform float depth;
uniform float relief;
float pv_lightAngle() { return lightAngle; }
float pv_depth() { return depth; }
float pv_relief() { return relief; }

vec3 blendOverlay(vec3 b, vec3 l) {
    return mix(2.0 * b * l, 1.0 - 2.0 * (1.0 - b) * (1.0 - l), step(0.5, b));
}

vec3 blendSoftLight(vec3 b, vec3 l) {
    return mix(2.0 * b * l + b * b * (1.0 - 2.0 * l), sqrt(b) * (2.0 * l - 1.0) + 2.0 * b * (1.0 - l), step(0.5, l));
}

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

float n_n2_l(vec2 uv) {
    return luma(n_n1_image(uv));
}

vec2 n_n3_grad(vec2 uv) {
    vec2 h = max(1.5, 0.01) / u_resolution;
    vec2 g = vec2(n_n2_l(uv + vec2(h.x, 0.0)) - n_n2_l(uv + vec2(-h.x, 0.0)), n_n2_l(uv + vec2(0.0, h.y)) - n_n2_l(uv + vec2(0.0, -h.y))) / (2.0 * h);
    return g;
}

vec2 n_n4_p(vec2 uv) {
    return rot2(radians(pv_lightAngle())) * vec2(1.0, 0.0);
}

float n_n5_dot(vec2 uv) {
    return dot(n_n3_grad(uv), n_n4_p(uv));
}

float n_n6_value(vec2 uv) {
    return n_n5_dot(uv) * pv_depth();
}

float n_n7_value(vec2 uv) {
    return n_n6_value(uv) + 0.5;
}

vec4 n_n8_image(vec2 uv) {
    vec4 b = n_n1_image(uv);
    vec4 l = vec4(vec3(n_n7_value(uv)), 1.0);
    vec3 rgb = blendOverlay(b.rgb, l.rgb);
    return vec4(mix(b.rgb, rgb, clamp(pv_relief() * l.a, 0.0, 1.0)), b.a);
}

void main() {
    fragColor = n_n8_image(v_texCoord);
}
