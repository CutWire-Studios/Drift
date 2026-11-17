#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_black;
uniform float k_n2_white;
uniform float k_n2_gamma;
uniform float k_n2_outBlack;
uniform float k_n2_outWhite;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 rgb = clamp((col.rgb - k_n2_black) / max(k_n2_white - k_n2_black, 0.0001), 0.0, 1.0);
    rgb = pow(rgb, vec3(1.0 / max(k_n2_gamma, 0.0001)));
    return vec4(mix(vec3(k_n2_outBlack), vec3(k_n2_outWhite), rgb), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
