#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform float intensity;
float pv_intensity() { return intensity; }
uniform vec4 k_n2_tint;
uniform float k_n3_amount;

vec4 n_n1_image(vec2 uv) {
    return texture(u_texture1, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 g = texture(u_currentTexture, uv).rgb * k_n2_tint.rgb * pv_intensity();
    return vec4(clamp(col.rgb + g, 0.0, 1.0), col.a);
}

vec4 n_n3_image(vec2 uv) {
    vec4 col = n_n2_image(uv);
    float t = k_n3_amount;
    vec3 rgb = col.rgb * vec3(1.0 + 0.25 * t, 1.0 + 0.05 * t, 1.0 - 0.25 * t);
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}

void main() {
    fragColor = n_n3_image(v_texCoord);
}
