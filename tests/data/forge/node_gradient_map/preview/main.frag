#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec4 k_n2_shadow;
uniform vec4 k_n2_highlight;
uniform float k_n2_amount;

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 duo = mix(k_n2_shadow.rgb, k_n2_highlight.rgb, smoothstep(0.0, 1.0, luma(col)));
    return vec4(mix(col.rgb, duo, k_n2_amount), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
