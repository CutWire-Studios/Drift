#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;
uniform float k_n2_length;
uniform float k_n2_min;
uniform float k_n2_max;

float n_n2_value(vec2 uv) {
    float t = fract(u_time / max(k_n2_length, 0.0001));
    float y;
    if (t <= 0.0) y = 0.0;
    else if (t < 1.0) { float k = (t - 0.0) / 1.0; y = mix(0.0, 1.0, k * k * (3.0 - 2.0 * k)); }
    else y = 1.0;
    return mix(k_n2_min, k_n2_max, y);
}

void main() {
    fragColor = vec4(vec3(n_n2_value(v_texCoord)), 1.0);
}
