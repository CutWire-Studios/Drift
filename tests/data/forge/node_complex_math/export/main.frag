#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

vec2 x_n2cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 x_n2cdiv(vec2 a, vec2 b) { float d = max(dot(b, b), 1e-8); return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / d; }

float n_n2_abs(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 sc = vec2(asp, -1.0) * 2.0;
    vec2 z = (uv - 0.5) * sc;
    vec2 w = vec2(1.0, 0.0);
    float n = 2.0;
    vec2 r = pow(max(length(z), 1e-8), n) * vec2(cos(n * atan(z.y, z.x)), sin(n * atan(z.y, z.x)));
    return length(r);
}

void main() {
    fragColor = vec4(vec3(n_n2_abs(v_texCoord)), 1.0);
}
