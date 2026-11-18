#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec4 k_n4_tint;
uniform float k_n4_intensity;

vec4 n_n3_image(vec2 uv) {
    return texture(u_texture1, uv);
}

vec4 n_n4_image(vec2 uv) {
    vec4 col = n_n3_image(uv);
    vec3 g = texture(u_currentTexture, uv).rgb * k_n4_tint.rgb * k_n4_intensity;
    return vec4(clamp(col.rgb + g, 0.0, 1.0), col.a);
}

void main() {
    fragColor = n_n4_image(v_texCoord);
}
