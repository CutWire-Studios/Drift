#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_amount;

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 x_n2_grad(float t) {
    if (t <= 0.0) return vec4(0.05, 0.02, 0.15, 1.0);
    if (t < 0.5) return mix(vec4(0.05, 0.02, 0.15, 1.0), vec4(0.85, 0.2, 0.35, 1.0), (t - 0.0) / 0.5);
    if (t < 1.0) return mix(vec4(0.85, 0.2, 0.35, 1.0), vec4(1.0, 0.85, 0.4, 1.0), (t - 0.5) / 0.5);
    return vec4(1.0, 0.85, 0.4, 1.0);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    float t = clamp(luma(col), 0.0, 1.0);
    vec4 g = x_n2_grad(t);
    return vec4(mix(col.rgb, g.rgb, k_n2_amount * g.a), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
