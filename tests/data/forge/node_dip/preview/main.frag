#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform float u_progress;
uniform float k_n2_hold;
uniform vec4 k_n2_color;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n4_image(vec2 uv) {
    return texture(u_texture1, uv);
}

vec4 n_n2_image(vec2 uv) {
    float p = clamp(u_progress, 0.0, 1.0);
    float h = k_n2_hold * 0.5;
    vec4 dip = vec4(k_n2_color.rgb, 1.0);
    if (p < 0.5 - h) return mix(n_n1_image(uv), dip, smoothstep(0.0, 0.5 - h, p));
    if (p > 0.5 + h) return mix(dip, n_n4_image(uv), smoothstep(0.5 + h, 1.0, p));
    return dip;
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
