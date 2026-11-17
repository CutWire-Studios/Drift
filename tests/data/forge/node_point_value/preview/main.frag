#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 k_n2_point;

vec2 n_n2_point(vec2 uv) {
    return k_n2_point;
}

void main() {
    fragColor = vec4(n_n2_point(v_texCoord), 0.0, 1.0);
}
