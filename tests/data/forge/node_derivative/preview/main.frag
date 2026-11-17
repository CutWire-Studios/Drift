#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float k_n2_spacing;
uniform float k_n2_x;

float n_n2_dx(vec2 uv) {
    vec2 h = max(k_n2_spacing, 0.01) / u_resolution;
    vec2 g = vec2(k_n2_x - k_n2_x, k_n2_x - k_n2_x) / (2.0 * h);
    return g.x;
}

void main() {
    fragColor = vec4(vec3(n_n2_dx(v_texCoord)), 1.0);
}
