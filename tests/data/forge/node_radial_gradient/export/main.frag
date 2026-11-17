#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    float t = clamp(length((uv - vec2(0.5, 0.5)) * vec2(asp, 1.0)) / max(0.5, 0.0001), 0.0, 1.0);
    return mix(n_n1_image(uv), vec4(0.0, 0.0, 0.0, 1.0), t);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
