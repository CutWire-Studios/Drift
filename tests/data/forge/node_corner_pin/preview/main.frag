#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 k_n2_tl;
uniform vec2 k_n2_tr;
uniform vec2 k_n2_br;
uniform vec2 k_n2_bl;

float cross2(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }

// Where p sits inside the quad a-b-c-d (clockwise from top-left) as 0..1 coordinates; outside
// the quad the result leaves 0..1. After Inigo Quilez's inverse bilinear interpolation.
vec2 invBilinear(vec2 p, vec2 a, vec2 b, vec2 c, vec2 d) {
    vec2 e = b - a;
    vec2 f = d - a;
    vec2 g = a - b + c - d;
    vec2 h = p - a;
    float k2 = cross2(g, f);
    float k1 = cross2(e, f) + cross2(h, g);
    float k0 = cross2(h, e);
    if (abs(k2) < 0.0001) return vec2((h.x * k1 + f.x * k0) / (e.x * k1 - g.x * k0), -k0 / k1);
    float w = k1 * k1 - 4.0 * k0 * k2;
    if (w < 0.0) return vec2(-1.0);
    w = sqrt(w);
    float ik2 = 0.5 / k2;
    float v = (-k1 - w) * ik2;
    float u = (h.x - f.x * v) / (e.x + g.x * v);
    if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) {
        v = (-k1 + w) * ik2;
        u = (h.x - f.x * v) / (e.x + g.x * v);
    }
    return vec2(u, v);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec2 q = invBilinear(uv, k_n2_tl, k_n2_tr, k_n2_br, k_n2_bl);
    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) return vec4(0.0);
    return n_n1_image(q);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
