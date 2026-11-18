#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;
uniform float k_n2_amount;
uniform float k_n2_rate;
uniform float k_n2_seed;

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float n_n2_value(vec2 uv) {
    return 1.0 - k_n2_amount * hash11(floor(u_time * k_n2_rate) + 0.5 + k_n2_seed * 13.1);
}

void main() {
    fragColor = vec4(vec3(n_n2_value(v_texCoord)), 1.0);
}
