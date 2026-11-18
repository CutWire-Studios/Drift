#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 b = n_n1_image(uv);
    vec4 l = vec4(1.0, 1.0, 1.0, 1.0);
    vec3 rgb = 1.0 - (1.0 - b.rgb) * (1.0 - l.rgb);
    return vec4(mix(b.rgb, rgb, clamp(1.0 * l.a, 0.0, 1.0)), b.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
