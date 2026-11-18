#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_x;
uniform float k_n2_y;

vec2 n_n2_p(vec2 uv) {
    return vec2(k_n2_x, k_n2_y);
}

void main() {
    fragColor = vec4(n_n2_p(v_texCoord), 0.0, 1.0);
}
