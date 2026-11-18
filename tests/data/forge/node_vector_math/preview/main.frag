#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 k_n2_a;
uniform vec2 k_n2_b;
uniform float k_n2_s;

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

vec2 n_n2_p(vec2 uv) {
    return k_n2_a + k_n2_b;
}

void main() {
    fragColor = vec4(n_n2_p(v_texCoord), 0.0, 1.0);
}
