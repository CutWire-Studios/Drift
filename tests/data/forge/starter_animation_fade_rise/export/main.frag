#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float u_direction;
uniform float rise;
float pv_rise() { return rise; }

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

float n_n2_settled(vec2 uv) {
    return mix(u_progress, 1.0 - u_progress, u_direction);
}

float n_n3_value(vec2 uv) {
    return mix(pv_rise(), 0.0, n_n2_settled(uv));
}

vec2 n_n4_p(vec2 uv) {
    return vec2(0.0, n_n3_value(uv));
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n5_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 p = uv - n_n4_p(uv) - vec2(0.5, 0.5);
    p.x *= asp;
    p = rot2(-radians(0.0)) * p;
    p /= max(1.0, 0.0001);
    p.x /= asp;
    p += vec2(0.5, 0.5);
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
