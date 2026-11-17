#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform vec2 k_n2_center;
uniform float k_n2_angle;

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 ctr = k_n2_center;
    vec2 d = (uv - ctr) * vec2(asp, 1.0);
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 24; ++k) {
        float a = radians(k_n2_angle) * (float(k) / 23.0 - 0.5);
        vec2 q = rot2(a) * d;
        sum += n_n1_image(ctr + vec2(q.x / asp, q.y));
    }
    return sum / 24.0;
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
