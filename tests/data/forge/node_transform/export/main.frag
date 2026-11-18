#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

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
    vec2 p = uv - vec2(0.0, 0.0) - vec2(0.5, 0.5);
    p.x *= asp;
    p = rot2(-radians(0.0)) * p;
    p /= max(1.0, 0.0001);
    p.x /= asp;
    p += vec2(0.5, 0.5);
    return n_n1_image(clamp(p, 0.0, 1.0));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
