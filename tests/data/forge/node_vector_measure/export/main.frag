#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

float n_n2_length(vec2 uv) {
    return length(vec2(1.0, 0.0));
}

void main() {
    fragColor = vec4(vec3(n_n2_length(v_texCoord)), 1.0);
}
