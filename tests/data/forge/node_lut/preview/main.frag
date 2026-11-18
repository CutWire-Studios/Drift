#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform float k_n2_amount;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 rgb = clamp(col.rgb, 0.0, 1.0);
    float b = rgb.b * 63.0;
    float s0 = floor(b);
    float s1 = min(s0 + 1.0, 63.0);
    vec3 a0 = texture(u_texture1, vec2(mod(s0, 8.0), floor(s0 / 8.0)) / 8.0 + (0.5 / 512.0) + rgb.rg * (63.0 / 512.0)).rgb;
    vec3 a1 = texture(u_texture1, vec2(mod(s1, 8.0), floor(s1 / 8.0)) / 8.0 + (0.5 / 512.0) + rgb.rg * (63.0 / 512.0)).rgb;
    vec3 graded = mix(a0, a1, b - s0);
    return vec4(mix(col.rgb, graded, k_n2_amount), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
