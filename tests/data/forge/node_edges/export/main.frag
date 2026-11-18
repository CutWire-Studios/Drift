#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec2 px = 1.0 / u_resolution;
    float gx = -luma(n_n1_image(uv + vec2(-1.0, -1.0) * px)) - 2.0 * luma(n_n1_image(uv + vec2(-1.0, 0.0) * px)) - luma(n_n1_image(uv + vec2(-1.0, 1.0) * px)) + luma(n_n1_image(uv + vec2(1.0, -1.0) * px)) + 2.0 * luma(n_n1_image(uv + vec2(1.0, 0.0) * px)) + luma(n_n1_image(uv + vec2(1.0, 1.0) * px));
    float gy = -luma(n_n1_image(uv + vec2(-1.0, -1.0) * px)) - 2.0 * luma(n_n1_image(uv + vec2(0.0, -1.0) * px)) - luma(n_n1_image(uv + vec2(1.0, -1.0) * px)) + luma(n_n1_image(uv + vec2(-1.0, 1.0) * px)) + 2.0 * luma(n_n1_image(uv + vec2(0.0, 1.0) * px)) + luma(n_n1_image(uv + vec2(1.0, 1.0) * px));
    float e = clamp(length(vec2(gx, gy)) * 2.0, 0.0, 1.0);
    return vec4(vec3(e), n_n1_image(uv).a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
