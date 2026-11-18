#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float k_n2_size;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    float cs = max(k_n2_size, 1.0) / u_resolution.y;
    vec2 p = uv * vec2(asp, 1.0) / cs;
    vec2 s = vec2(1.0, 1.7320508);
    vec2 a = mod(p, s) - s * 0.5;
    vec2 b = mod(p - s * 0.5, s) - s * 0.5;
    vec2 g = dot(a, a) < dot(b, b) ? a : b;
    vec2 q = (p - g) * cs / vec2(asp, 1.0);
    return n_n1_image(clamp(q, 0.0, 1.0));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
