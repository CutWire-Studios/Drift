#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

vec2 n_n2_p(vec2 uv) {
    return vec2(0.0, 0.0) + vec2(0.0, 0.0);
}

void main() {
    fragColor = vec4(n_n2_p(v_texCoord), 0.0, 1.0);
}
