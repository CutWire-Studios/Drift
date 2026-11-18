#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;
uniform float k_n2_seed;
uniform float k_n2_rate;
uniform float k_n2_phase;
uniform float k_n2_min;
uniform float k_n2_max;

float n_n2_value(vec2 uv) {
    float x = u_time * k_n2_rate + k_n2_phase;
    return mix(k_n2_min, k_n2_max, 0.5 - 0.5 * cos(x * 6.2832));
}

void main() {
    fragColor = vec4(vec3(n_n2_value(v_texCoord)), 1.0);
}
