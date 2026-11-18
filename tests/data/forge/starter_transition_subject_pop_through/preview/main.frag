#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float punch;
uniform float split;
uniform float holdUntil;
float pv_punch() { return punch; }
float pv_split() { return split; }
float pv_holdUntil() { return holdUntil; }
uniform float k_n11_angle;
uniform vec2 k_n10_center;
uniform float k_n12_high;

float n_n4_value(vec2 uv) {
    return u_progress;
}

float n_n6_value(vec2 uv) {
    float t = clamp(n_n4_value(uv), 0.0, 1.0);
    return sin(t * 3.14159265);
}

float n_n8_value(vec2 uv) {
    return n_n6_value(uv) * pv_split();
}

float n_n7_value(vec2 uv) {
    return n_n6_value(uv) * pv_punch();
}

vec4 n_n10_image(vec2 uv) {
    vec2 ctr = k_n10_center;
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 24; ++k) {
        float s = 1.0 - n_n7_value(uv) * float(k) / 23.0;
        sum += texture(u_currentTexture, ctr + (uv - ctr) * s);
    }
    return sum / 24.0;
}

vec4 n_n11_image(vec2 uv) {
    float a = radians(k_n11_angle);
    vec2 delta = vec2(cos(a), sin(a)) * n_n8_value(uv) / u_resolution;
    vec4 g = n_n10_image(uv);
    float r = n_n10_image(clamp(uv + delta, 0.0, 1.0)).r;
    float b = n_n10_image(clamp(uv - delta, 0.0, 1.0)).b;
    return vec4(r, g.g, b, g.a);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_texture1, uv);
}

float n_n3_mask(vec2 uv) {
    return driftMask(uv);
}

float n_n12_value(vec2 uv) {
    return smoothstep(pv_holdUntil(), max(k_n12_high, pv_holdUntil() + 1e-5), n_n4_value(uv));
}

float n_n13_value(vec2 uv) {
    return 1.0 - n_n12_value(uv);
}

float n_n14_value(vec2 uv) {
    return n_n3_mask(uv) * n_n13_value(uv);
}

vec4 n_n15_image(vec2 uv) {
    return mix(n_n11_image(uv), n_n1_image(uv), clamp(n_n14_value(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n15_image(v_texCoord);
}
