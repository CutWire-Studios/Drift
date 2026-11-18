#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float bps;
float pv_bps() { return bps; }
uniform vec2 k_n5_center;
uniform float k_n3_seed;
uniform float k_n3_phase;
uniform float k_n3_min;
uniform float k_n3_max;
uniform vec2 k_n4_center;
uniform float k_n2_seed;
uniform float k_n2_phase;
uniform float k_n2_min;
uniform float k_n2_max;

float n_n3_value(vec2 uv) {
    float x = u_time * pv_bps() + k_n3_phase;
    return mix(k_n3_min, k_n3_max, exp(-fract(x) * 6.0));
}

float n_n2_value(vec2 uv) {
    float x = u_time * pv_bps() + k_n2_phase;
    return mix(k_n2_min, k_n2_max, exp(-fract(x) * 6.0));
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n4_image(vec2 uv) {
    vec2 p = (uv - k_n4_center) / max(n_n2_value(uv), 0.0001) + k_n4_center;
    return n_n1_image(clamp(p, 0.0, 1.0));
}

vec4 n_n5_image(vec2 uv) {
    vec2 delta = (uv - k_n5_center) * 2.0 * n_n3_value(uv) / u_resolution;
    vec4 g = n_n4_image(uv);
    float r = n_n4_image(clamp(uv + delta, 0.0, 1.0)).r;
    float b = n_n4_image(clamp(uv - delta, 0.0, 1.0)).b;
    return vec4(r, g.g, b, g.a);
}

void main() {
    fragColor = n_n5_image(v_texCoord);
}
