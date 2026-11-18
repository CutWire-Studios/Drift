#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float power;
uniform float zoom;
float pv_power() { return power; }
float pv_zoom() { return zoom; }
uniform vec2 k_n2_w;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec2 x_n2cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 x_n2cdiv(vec2 a, vec2 b) { float d = max(dot(b, b), 1e-8); return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / d; }

vec2 n_n2_p(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 sc = vec2(asp, -1.0) * pv_zoom();
    vec2 z = (uv - 0.5) * sc;
    vec2 w = k_n2_w;
    float n = pv_power();
    vec2 r = pow(max(length(z), 1e-8), n) * vec2(cos(n * atan(z.y, z.x)), sin(n * atan(z.y, z.x)));
    return r / sc + 0.5;
}

vec4 n_n3_image(vec2 uv) {
    vec2 p = n_n2_p(uv);
    return n_n1_image(1.0 - abs(mod(p, 2.0) - 1.0));
}

void main() {
    fragColor = n_n3_image(v_texCoord);
}
