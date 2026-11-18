#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float focus;
uniform float blurAmount;
float pv_focus() { return focus; }
float pv_blurAmount() { return blurAmount; }
uniform float k_n2_angle;
uniform vec4 k_n2_a;
uniform vec4 k_n2_b;
uniform float k_n5_inMin;
uniform float k_n5_inMax;
uniform float k_n5_outMin;

float n_n2_t(vec2 uv) {
    float ang = radians(k_n2_angle);
    vec2 d = vec2(cos(ang), sin(ang));
    float t = clamp(dot(uv - 0.5, d) / max(abs(d.x) + abs(d.y), 0.0001) + 0.5, 0.0, 1.0);
    return t;
}

float n_n3_value(vec2 uv) {
    return n_n2_t(uv) - pv_focus();
}

float n_n4_value(vec2 uv) {
    return abs(n_n3_value(uv));
}

float n_n5_value(vec2 uv) {
    float t = clamp((n_n4_value(uv) - k_n5_inMin) / (k_n5_inMax - k_n5_inMin + 1e-6), 0.0, 1.0);
    return mix(k_n5_outMin, pv_blurAmount(), t);
}

vec4 s_n6_1(vec2 uv) {
    float r = max(n_n5_value(uv), 0.0);
    float sigma = max(r * 0.5, 0.0001);
    float stepPx = max(r / 16.0, 1.0);
    vec2 texel = vec2(0.0, 1.0) / u_resolution;
    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    for (int k = -16; k <= 16; ++k) {
        float o = float(k) * stepPx;
        if (abs(o) > r + 0.001) continue;
        float w = exp(-0.5 * o * o / (sigma * sigma));
        sum += texture(u_currentTexture, uv + texel * o) * w;
        wsum += w;
    }
    return sum / wsum;
}

void main() {
    fragColor = s_n6_1(v_texCoord);
}
