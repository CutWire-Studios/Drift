#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float u_time;
uniform float bps;
uniform float punch;
uniform float split;
float pv_bps() { return bps; }
float pv_punch() { return punch; }
float pv_split() { return split; }
uniform float k_n11_angle;
uniform float k_n3_seed;
uniform float k_n3_phase;
uniform float k_n3_min;
uniform float k_n3_max;
uniform float k_n5_b;
uniform vec2 k_n10_center;

float n_n3_value(vec2 uv) {
    float x = u_time * pv_bps() + k_n3_phase;
    return mix(k_n3_min, k_n3_max, fract(x));
}

float n_n4_value(vec2 uv) {
    return 1.0 - n_n3_value(uv);
}

float n_n5_value(vec2 uv) {
    return pow(abs(n_n4_value(uv)), k_n5_b);
}

float n_n8_value(vec2 uv) {
    return n_n5_value(uv) * pv_split();
}

float n_n6_value(vec2 uv) {
    return n_n5_value(uv) * pv_punch();
}

vec4 n_n10_image(vec2 uv) {
    vec2 ctr = k_n10_center;
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 24; ++k) {
        float s = 1.0 - n_n6_value(uv) * float(k) / 23.0;
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

float n_n2_mask(vec2 uv) {
    return driftMask(uv);
}

vec4 n_n12_image(vec2 uv) {
    return mix(n_n11_image(uv), n_n1_image(uv), clamp(n_n2_mask(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n12_image(v_texCoord);
}
