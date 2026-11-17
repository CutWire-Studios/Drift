#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float k_n2_speed;
uniform float k_n2_seed;
uniform float k_n2_rotation;
uniform float k_n2_amount;

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

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float ts = u_time * k_n2_speed + k_n2_seed * 11.3;
    vec2 j = vec2(valueNoise(vec2(ts, 3.7)), valueNoise(vec2(9.1, ts))) - 0.5;
    float tw = (valueNoise(vec2(ts * 0.9, 5.3)) - 0.5) * 2.0 * radians(k_n2_rotation);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 q = (uv - 0.5) * vec2(asp, 1.0);
    q = rot2(tw) * q;
    vec2 p = vec2(q.x / asp, q.y) / (1.0 + k_n2_amount * 2.0 + abs(tw) * 0.5) + 0.5 + j * 2.0 * k_n2_amount;
    return n_n1_image(clamp(p, 0.0, 1.0));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
