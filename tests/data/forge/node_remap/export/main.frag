#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

float n_n2_value(vec2 uv) {
    float t = clamp((0.5 - 0.0) / (1.0 - 0.0 + 1e-6), 0.0, 1.0);
    return mix(0.0, 1.0, t);
}

void main() {
    fragColor = vec4(vec3(n_n2_value(v_texCoord)), 1.0);
}
