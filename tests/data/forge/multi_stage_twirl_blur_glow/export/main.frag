#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;

vec4 n_n3_image(vec2 uv) {
    return texture(u_texture1, uv);
}

vec4 n_n4_image(vec2 uv) {
    vec4 col = n_n3_image(uv);
    vec3 g = texture(u_currentTexture, uv).rgb * vec4(1.0, 1.0, 1.0, 1.0).rgb * 1.0;
    return vec4(clamp(col.rgb + g, 0.0, 1.0), col.a);
}

void main() {
    fragColor = n_n4_image(v_texCoord);
}
