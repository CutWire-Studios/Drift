#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 s_n2_0(vec2 uv) {
    vec4 col = n_n1_image(uv);
    return vec4(max(col.rgb - vec3(0.6), 0.0) / max(1.0 - 0.6, 0.0001), 1.0);
}

void main() {
    fragColor = s_n2_0(v_texCoord);
}
