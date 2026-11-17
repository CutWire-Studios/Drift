#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec4 k_n2_b;
uniform float k_n2_amount;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return mix(n_n1_image(uv), k_n2_b, clamp(k_n2_amount, 0.0, 1.0));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
