#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;
uniform float k_n2_speed;
uniform float k_n2_frequency;
uniform float k_n2_amplitude;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec2 p = uv + vec2(sin(uv.y * k_n2_frequency * 6.2832 + u_time * k_n2_speed) * k_n2_amplitude, 0.0);
    return n_n1_image(clamp(p, 0.0, 1.0));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
