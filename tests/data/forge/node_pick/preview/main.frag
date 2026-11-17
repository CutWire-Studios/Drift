#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_which;
uniform vec4 k_n2_b;
uniform vec4 k_n2_c;
uniform vec4 k_n2_d;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float w = floor(k_n2_which + 0.5);
    if (w < 0.5) return n_n1_image(uv);
    if (w < 1.5) return k_n2_b;
    if (w < 2.5) return k_n2_c;
    return k_n2_d;
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
