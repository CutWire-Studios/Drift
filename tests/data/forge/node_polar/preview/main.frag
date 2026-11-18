#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform vec2 k_n2_center;
uniform float k_n2_zoom;
uniform float k_n2_rotation;
uniform float k_n2_repeat;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 d = (uv - k_n2_center) * vec2(asp, 1.0);
    float r = length(d) * 2.0 / max(k_n2_zoom, 0.0001);
    float a = atan(d.y, d.x) / 6.2831853 + 0.5 + k_n2_rotation / 360.0;
    return n_n1_image(vec2(fract(a * max(floor(k_n2_repeat), 1.0)), clamp(1.0 - r, 0.0, 1.0)));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
