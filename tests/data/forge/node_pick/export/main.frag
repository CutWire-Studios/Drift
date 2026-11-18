#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float w = floor(0.0 + 0.5);
    if (w < 0.5) return n_n1_image(uv);
    if (w < 1.5) return vec4(0.0, 0.0, 0.0, 1.0);
    if (w < 2.5) return vec4(0.0, 0.0, 0.0, 1.0);
    return vec4(0.0, 0.0, 0.0, 1.0);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
