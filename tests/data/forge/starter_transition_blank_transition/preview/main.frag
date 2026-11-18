#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform float u_progress;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return texture(u_texture1, uv);
}

float n_n3_value(vec2 uv) {
    return u_progress;
}

vec4 n_n4_image(vec2 uv) {
    return mix(n_n1_image(uv), n_n2_image(uv), clamp(n_n3_value(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n4_image(v_texCoord);
}
