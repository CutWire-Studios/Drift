#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n2_color(vec2 uv) {
    return vec4(0.0, 0.0, 0.0, 1.0);
}

void main() {
    fragColor = n_n2_color(v_texCoord);
}
