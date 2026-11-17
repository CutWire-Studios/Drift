#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;
uniform float k_n2_steps;

float n_n2_value(vec2 uv) {
    return floor(u_time * k_n2_steps) / max(k_n2_steps, 1.0);
}

void main() {
    fragColor = vec4(vec3(n_n2_value(v_texCoord)), 1.0);
}
