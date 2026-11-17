#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_rr;
uniform float k_n2_rg;
uniform float k_n2_rb;
uniform float k_n2_gr;
uniform float k_n2_gg;
uniform float k_n2_gb;
uniform float k_n2_br;
uniform float k_n2_bg;
uniform float k_n2_bb;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 rgb = vec3(
        dot(col.rgb, vec3(k_n2_rr, k_n2_rg, k_n2_rb)),
        dot(col.rgb, vec3(k_n2_gr, k_n2_gg, k_n2_gb)),
        dot(col.rgb, vec3(k_n2_br, k_n2_bg, k_n2_bb)));
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
