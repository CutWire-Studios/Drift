#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_on;

float n_n2_value(vec2 uv) {
    return k_n2_on > 0.5 ? 1.0 : 0.0;
}

void main() {
    fragColor = vec4(vec3(n_n2_value(v_texCoord)), 1.0);
}
