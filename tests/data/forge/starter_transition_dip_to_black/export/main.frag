#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform float u_progress;
uniform vec3 dipColor;
vec3 pv_dipColor() { return dipColor; }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return texture(u_texture1, uv);
}

vec4 n_n3_image(vec2 uv) {
    float p = clamp(u_progress, 0.0, 1.0);
    float h = 0.1 * 0.5;
    vec4 dip = vec4(vec4(pv_dipColor(), 1.0).rgb, 1.0);
    if (p < 0.5 - h) return mix(n_n1_image(uv), dip, smoothstep(0.0, 0.5 - h, p));
    if (p > 0.5 + h) return mix(dip, n_n2_image(uv), smoothstep(0.5 + h, 1.0, p));
    return dip;
}

void main() {
    fragColor = n_n3_image(v_texCoord);
}
