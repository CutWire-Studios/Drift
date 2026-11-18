#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

float n_n2_det(vec2 uv) {
    return determinant(mat2(1.0, 0.0, 0.0, 1.0));
}

void main() {
    fragColor = vec4(vec3(n_n2_det(v_texCoord)), 1.0);
}
