#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float k_n2_angle;
uniform float k_n2_length;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float a = radians(k_n2_angle);
    vec2 dir = vec2(cos(a), sin(a)) * k_n2_length / u_resolution;
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 24; ++k) {
        float t = float(k) / 23.0 - 0.5;
        sum += n_n1_image(uv + dir * t);
    }
    return sum / 24.0;
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
