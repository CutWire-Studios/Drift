#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

float n_n2_dx(vec2 uv) {
    vec2 h = max(1.0, 0.01) / u_resolution;
    vec2 g = vec2(0.0 - 0.0, 0.0 - 0.0) / (2.0 * h);
    return g.x;
}

void main() {
    fragColor = vec4(vec3(n_n2_dx(v_texCoord)), 1.0);
}
