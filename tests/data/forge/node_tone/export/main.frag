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
    float l = luma(col);
    float wS = 1.0 - smoothstep(0.0, 0.55, l);
    float wH = smoothstep(0.45, 1.0, l);
    float wB = 1.0 - smoothstep(0.0, 0.25, l);
    float wW = smoothstep(0.75, 1.0, l);
    float d = 0.2 * 0.5 * wS + -0.1 * 0.5 * wH + 0.0 * 0.3 * wB + 0.0 * 0.3 * wW;
    return vec4(clamp(col.rgb + d, 0.0, 1.0), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
