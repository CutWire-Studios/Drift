#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;
uniform float bps;
float pv_bps() { return bps; }

float n_n3_value(vec2 uv) {
    float x = u_time * pv_bps() + 0.0;
    return mix(0.0, 1.0, fract(x));
}

float n_n4_value(vec2 uv) {
    return 1.0 - n_n3_value(uv);
}

float n_n5_value(vec2 uv) {
    return pow(abs(n_n4_value(uv)), 3.0);
}

float n_n7_value(vec2 uv) {
    float t = clamp((n_n5_value(uv) - 0.0) / (1.0 - 0.0 + 1e-6), 0.0, 1.0);
    return mix(1.0, 1.08, t);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n9_image(vec2 uv) {
    vec2 p = (uv - vec2(0.5, 0.5)) / max(n_n7_value(uv), 0.0001) + vec2(0.5, 0.5);
    return n_n1_image(clamp(p, 0.0, 1.0));
}

void main() {
    fragColor = n_n9_image(v_texCoord);
}
