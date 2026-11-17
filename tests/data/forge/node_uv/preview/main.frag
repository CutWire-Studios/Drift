#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

float n_n2_x(vec2 uv) {
    return uv.x;
}

void main() {
    fragColor = vec4(vec3(n_n2_x(v_texCoord)), 1.0);
}
