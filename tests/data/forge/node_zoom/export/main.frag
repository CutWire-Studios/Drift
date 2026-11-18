#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec2 p = (uv - vec2(0.5, 0.5)) / max(1.5, 0.0001) + vec2(0.5, 0.5);
    return n_n1_image(clamp(p, 0.0, 1.0));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
