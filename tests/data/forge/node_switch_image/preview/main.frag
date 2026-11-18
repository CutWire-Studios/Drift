#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_on;
uniform vec4 k_n2_b;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return k_n2_on > 0.5 ? k_n2_b : n_n1_image(uv);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
