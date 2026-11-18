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
    vec2 d = (uv - vec2(0.5, 0.5)) * vec2(asp, 1.0);
    float r = length(d) * 2.0 / max(1.0, 0.0001);
    float a = atan(d.y, d.x) / 6.2831853 + 0.5 + 0.0 / 360.0;
    return n_n1_image(vec2(fract(a * max(floor(1.0), 1.0)), clamp(1.0 - r, 0.0, 1.0)));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
