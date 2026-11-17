#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float swirlSize;
uniform float flowSpeed;
uniform float flow;
float pv_swirlSize() { return swirlSize; }
float pv_flowSpeed() { return flowSpeed; }
float pv_flow() { return flow; }

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec2 n_n6_uv(vec2 uv) {
    return uv;
}

float n_n2_value(vec2 uv) {
    return valueNoise(uv * vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0) * pv_swirlSize() + vec2(u_time * pv_flowSpeed(), u_time * pv_flowSpeed() * 0.7) + vec2(0.0 * 17.31, 0.0 * 7.77));
}

vec2 n_n3_grad(vec2 uv) {
    vec2 h = max(2.0, 0.01) / u_resolution;
    vec2 g = vec2(n_n2_value(uv + vec2(h.x, 0.0)) - n_n2_value(uv + vec2(-h.x, 0.0)), n_n2_value(uv + vec2(0.0, h.y)) - n_n2_value(uv + vec2(0.0, -h.y))) / (2.0 * h);
    return g;
}

vec2 n_n4_p(vec2 uv) {
    return vec2(-n_n3_grad(uv).y, n_n3_grad(uv).x);
}

vec2 n_n5_p(vec2 uv) {
    return n_n4_p(uv) * pv_flow();
}

vec2 n_n7_p(vec2 uv) {
    return n_n6_uv(uv) + n_n5_p(uv);
}

vec4 n_n8_image(vec2 uv) {
    vec2 p = n_n7_p(uv);
    return n_n1_image(1.0 - abs(mod(p, 2.0) - 1.0));
}

void main() {
    fragColor = n_n8_image(v_texCoord);
}
