#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float focus;
uniform float blurAmount;
float pv_focus() { return focus; }
float pv_blurAmount() { return blurAmount; }

float n_n2_t(vec2 uv) {
    float ang = radians(90.0);
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
    float t = clamp((n_n4_value(uv) - 0.08) / (0.4 - 0.08 + 1e-6), 0.0, 1.0);
    return mix(0.0, pv_blurAmount(), t);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 s_n6_0(vec2 uv) {
    float r = max(n_n5_value(uv), 0.0);
    float sigma = max(r * 0.5, 0.0001);
    float stepPx = max(r / 16.0, 1.0);
    vec2 texel = vec2(1.0, 0.0) / u_resolution;
    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    for (int k = -16; k <= 16; ++k) {
        float o = float(k) * stepPx;
        if (abs(o) > r + 0.001) continue;
        float w = exp(-0.5 * o * o / (sigma * sigma));
        sum += n_n1_image(uv + texel * o) * w;
        wsum += w;
    }
    return sum / wsum;
}

void main() {
    fragColor = s_n6_0(v_texCoord);
}
