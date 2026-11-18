#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float m = clamp(0.0, 0.0, 1.0);
    float band = pow(1.0 - abs(m * 2.0 - 1.0), 2.0);
    return vec4(n_n1_image(uv).rgb * band * 1.5, band);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
