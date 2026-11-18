#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

float n_n2_r(vec2 uv) {
    return n_n1_image(uv).r;
}

void main() {
    fragColor = vec4(vec3(n_n2_r(v_texCoord)), 1.0);
}
