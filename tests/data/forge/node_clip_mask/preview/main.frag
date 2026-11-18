#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

float n_n2_mask(vec2 uv) {
    return driftMask(uv);
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
