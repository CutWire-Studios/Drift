#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_x;
uniform float k_n2_inMin;
uniform float k_n2_inMax;
uniform float k_n2_outMin;
uniform float k_n2_outMax;

float n_n2_value(vec2 uv) {
    float t = clamp((k_n2_x - k_n2_inMin) / (k_n2_inMax - k_n2_inMin + 1e-6), 0.0, 1.0);
    return mix(k_n2_outMin, k_n2_outMax, t);
}

void main() {
    fragColor = vec4(vec3(n_n2_value(v_texCoord)), 1.0);
}
