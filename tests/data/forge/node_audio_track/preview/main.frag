#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float src_level;
uniform float src_bass;
uniform float src_beat;
uniform sampler2D src;
uniform float k_n2_x;

float n_n2_spectrum(vec2 uv) {
    return texture(src, vec2(k_n2_x, 0.25)).r;
}

void main() {
    fragColor = vec4(vec3(n_n2_spectrum(v_texCoord)), 1.0);
}
