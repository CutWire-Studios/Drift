#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float n_n2_value(vec2 uv) {
    return 1.0 - 0.4 * hash11(floor(u_time * 12.0) + 0.5 + 0.0 * 13.1);
}

void main() {
    fragColor = vec4(vec3(n_n2_value(v_texCoord)), 1.0);
}
