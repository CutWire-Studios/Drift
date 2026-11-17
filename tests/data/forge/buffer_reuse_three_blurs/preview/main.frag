#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n4_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

void main() {
    fragColor = n_n4_image(v_texCoord);
}
