#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    return vec4(mix(col.rgb, 1.0 - col.rgb, 1.0), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
