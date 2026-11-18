#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

float n_n2_det(vec2 uv) {
    vec2 h = max(1.0, 0.01) / u_resolution;
    vec2 jx = ((uv + vec2(h.x, 0.0)) - (uv + vec2(-h.x, 0.0))) / (2.0 * h.x);
    vec2 jy = ((uv + vec2(0.0, h.y)) - (uv + vec2(0.0, -h.y))) / (2.0 * h.y);
    return jx.x * jy.y - jy.x * jx.y;
}

void main() {
    fragColor = vec4(vec3(n_n2_det(v_texCoord)), 1.0);
}
