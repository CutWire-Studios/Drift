#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 tinted = luma(col) * vec4(1.0, 0.55, 0.2, 1.0).rgb * 1.6;
    return vec4(mix(col.rgb, clamp(tinted, 0.0, 1.0), 0.4), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
