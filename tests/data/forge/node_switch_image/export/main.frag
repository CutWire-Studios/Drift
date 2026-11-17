#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return 0.0 > 0.5 ? vec4(1.0, 1.0, 1.0, 1.0) : n_n1_image(uv);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
