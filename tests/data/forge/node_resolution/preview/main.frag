#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

float n_n2_aspect(vec2 uv) {
    return u_resolution.x / max(u_resolution.y, 1.0);
}

void main() {
    fragColor = vec4(vec3(n_n2_aspect(v_texCoord)), 1.0);
}
