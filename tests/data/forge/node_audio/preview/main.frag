#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_audioLevel;
uniform float u_audioBass;
uniform float u_audioBeat;

float n_n2_level(vec2 uv) {
    return u_audioLevel;
}

void main() {
    fragColor = vec4(vec3(n_n2_level(v_texCoord)), 1.0);
}
