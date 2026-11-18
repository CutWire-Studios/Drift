#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_strength;

float n_n2_depth(vec2 uv) {
    return driftDepth(uv);
}

void main() {
    fragColor = vec4(vec3(n_n2_depth(v_texCoord)), 1.0);
}
