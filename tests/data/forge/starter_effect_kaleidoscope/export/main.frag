#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float spin;
uniform float slices;
float pv_spin() { return spin; }
float pv_slices() { return slices; }

float n_n2_value(vec2 uv) {
    return u_time * pv_spin();
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n3_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 ctr = vec2(0.5, 0.5);
    vec2 d = (uv - ctr) * vec2(asp, 1.0);
    float seg = 6.2832 / max(floor(pv_slices()), 2.0);
    float a = atan(d.y, d.x) + radians(n_n2_value(uv));
    a = mod(a, seg);
    a = abs(a - seg * 0.5);
    vec2 nd = vec2(cos(a), sin(a)) * length(d);
    vec2 p = ctr + vec2(nd.x / asp, nd.y);
    return n_n1_image(1.0 - abs(mod(p, 2.0) - 1.0));
}

void main() {
    fragColor = n_n3_image(v_texCoord);
}
