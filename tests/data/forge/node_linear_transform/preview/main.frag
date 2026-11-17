#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float k_n2_m00;
uniform float k_n2_m10;
uniform float k_n2_m01;
uniform float k_n2_m11;
uniform vec2 k_n2_origin;
uniform vec2 k_n2_offset;

float n_n2_det(vec2 uv) {
    return determinant(mat2(k_n2_m00, k_n2_m10, k_n2_m01, k_n2_m11));
}

void main() {
    fragColor = vec4(vec3(n_n2_det(v_texCoord)), 1.0);
}
