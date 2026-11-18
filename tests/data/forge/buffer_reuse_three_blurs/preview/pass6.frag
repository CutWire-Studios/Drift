#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float k_n4_radius;

vec4 s_n4_1(vec2 uv) {
    float r = max(k_n4_radius, 0.0);
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
    fragColor = s_n4_1(v_texCoord);
}
