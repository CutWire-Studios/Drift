#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec2 n_n2_p(vec2 uv) {
    return vec2(0.5, 0.5);
}

void main() {
    fragColor = vec4(n_n2_p(v_texCoord), 0.0, 1.0);
}
