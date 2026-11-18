#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;

float n_n2_value(vec2 uv) {
    float x = u_time * 1.0 + 0.0;
    return mix(0.0, 1.0, 0.5 - 0.5 * cos(x * 6.2832));
}

void main() {
    fragColor = vec4(vec3(n_n2_value(v_texCoord)), 1.0);
}
