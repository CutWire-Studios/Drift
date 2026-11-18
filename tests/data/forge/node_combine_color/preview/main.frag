#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_r;
uniform float k_n2_g;
uniform float k_n2_b;
uniform float k_n2_a;

vec4 n_n2_color(vec2 uv) {
    return vec4(k_n2_r, k_n2_g, k_n2_b, k_n2_a);
}

void main() {
    fragColor = n_n2_color(v_texCoord);
}
