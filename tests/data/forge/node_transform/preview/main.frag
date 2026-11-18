#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform vec2 k_n2_offset;
uniform vec2 k_n2_pivot;
uniform float k_n2_rotation;
uniform float k_n2_scale;

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 p = uv - k_n2_offset - k_n2_pivot;
    p.x *= asp;
    p = rot2(-radians(k_n2_rotation)) * p;
    p /= max(k_n2_scale, 0.0001);
    p.x /= asp;
    p += k_n2_pivot;
    return n_n1_image(clamp(p, 0.0, 1.0));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
