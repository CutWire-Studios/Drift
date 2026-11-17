#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float k_n2_radius;
uniform float k_n2_softness;
uniform vec4 k_n2_color;
uniform float k_n2_amount;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec2 d = (uv - 0.5) * vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0);
    float v = smoothstep(k_n2_radius - k_n2_softness, k_n2_radius, length(d) * 1.2);
    return vec4(mix(col.rgb, k_n2_color.rgb, v * k_n2_amount), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
