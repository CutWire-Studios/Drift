#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_low;
uniform float k_n2_high;

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

float n_n2_mask(vec2 uv) {
    return smoothstep(k_n2_low, max(k_n2_high, k_n2_low + 0.0001), luma(n_n1_image(uv)));
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
