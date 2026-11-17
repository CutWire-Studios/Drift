#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 k_n2_a;
uniform vec2 k_n2_b;

float n_n2_length(vec2 uv) {
    return length(k_n2_a);
}

void main() {
    fragColor = vec4(vec3(n_n2_length(v_texCoord)), 1.0);
}
