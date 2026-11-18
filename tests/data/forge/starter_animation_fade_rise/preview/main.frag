#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float u_direction;
uniform float rise;
float pv_rise() { return rise; }
uniform float k_n4_x;
uniform float k_n3_b;
uniform vec2 k_n5_pivot;
uniform float k_n5_rotation;
uniform float k_n5_scale;

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

float n_n2_settled(vec2 uv) {
    return mix(u_progress, 1.0 - u_progress, u_direction);
}

float n_n3_value(vec2 uv) {
    return mix(pv_rise(), k_n3_b, n_n2_settled(uv));
}

vec2 n_n4_p(vec2 uv) {
    return vec2(k_n4_x, n_n3_value(uv));
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n5_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 p = uv - n_n4_p(uv) - k_n5_pivot;
    p.x *= asp;
    p = rot2(-radians(k_n5_rotation)) * p;
    p /= max(k_n5_scale, 0.0001);
    p.x /= asp;
    p += k_n5_pivot;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return n_n1_image(p);
}

vec4 n_n6_image(vec2 uv) {
    vec4 col = n_n5_image(uv);
    return vec4(col.rgb, col.a * clamp(n_n2_settled(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n6_image(v_texCoord);
}
