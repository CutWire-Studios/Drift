#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_low;
uniform float k_n2_high;
uniform float k_n2_x;

float n_n2_value(vec2 uv) {
    return smoothstep(k_n2_low, max(k_n2_high, k_n2_low + 1e-5), k_n2_x);
}

void main() {
    fragColor = vec4(vec3(n_n2_value(v_texCoord)), 1.0);
}
