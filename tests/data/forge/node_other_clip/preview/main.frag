#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D src;

vec4 n_n2_image(vec2 uv) {
    return texture(src, uv);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
