#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_a;
uniform float k_n2_b;
uniform float k_n2_t;

float n_n2_value(vec2 uv) {
    return mix(k_n2_a, k_n2_b, k_n2_t);
}

void main() {
    fragColor = vec4(vec3(n_n2_value(v_texCoord)), 1.0);
}
