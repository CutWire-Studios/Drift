#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 k_n2_count;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return n_n1_image(fract(uv * k_n2_count));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
