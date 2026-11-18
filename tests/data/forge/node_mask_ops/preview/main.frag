#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_a;
uniform float k_n2_b;

float n_n2_mask(vec2 uv) {
    return max(clamp(k_n2_a, 0.0, 1.0), clamp(k_n2_b, 0.0, 1.0));
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
