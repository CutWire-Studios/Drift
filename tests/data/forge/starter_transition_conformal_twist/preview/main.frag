#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float twist;
float pv_twist() { return twist; }
uniform float k_n7_zoom;
uniform vec2 k_n7_w;
uniform float k_n6_b;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec2 x_n7cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 x_n7cdiv(vec2 a, vec2 b) { float d = max(dot(b, b), 1e-8); return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / d; }

float n_n3_value(vec2 uv) {
    return u_progress;
}

float n_n4_value(vec2 uv) {
    float t = clamp(n_n3_value(uv), 0.0, 1.0);
    return sin(t * 3.14159265);
}

float n_n5_value(vec2 uv) {
    return n_n4_value(uv) * pv_twist();
}

float n_n6_value(vec2 uv) {
    return n_n5_value(uv) + k_n6_b;
}

vec2 n_n7_p(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 sc = vec2(asp, -1.0) * k_n7_zoom;
    vec2 z = (uv - 0.5) * sc;
    vec2 w = k_n7_w;
    float n = n_n6_value(uv);
    vec2 r = pow(max(length(z), 1e-8), n) * vec2(cos(n * atan(z.y, z.x)), sin(n * atan(z.y, z.x)));
    return r / sc + 0.5;
}

vec4 n_n8_image(vec2 uv) {
    vec2 p = n_n7_p(uv);
    return n_n1_image(1.0 - abs(mod(p, 2.0) - 1.0));
}

vec4 n_n2_image(vec2 uv) {
    return texture(u_texture1, uv);
}

vec4 n_n9_image(vec2 uv) {
    vec2 p = n_n7_p(uv);
    return n_n2_image(1.0 - abs(mod(p, 2.0) - 1.0));
}

float n_n10_value(vec2 uv) {
    float t = clamp(n_n3_value(uv), 0.0, 1.0);
    return t < 0.5 ? 4.0 * t * t * t : 1.0 - pow(-2.0 * t + 2.0, 3.0) / 2.0;
}

vec4 n_n11_image(vec2 uv) {
    return mix(n_n8_image(uv), n_n9_image(uv), clamp(n_n10_value(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n11_image(v_texCoord);
}
