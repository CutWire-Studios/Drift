#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float k_n2_angle;
uniform vec4 k_n2_b;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float ang = radians(k_n2_angle);
    vec2 d = vec2(cos(ang), sin(ang));
    float t = clamp(dot(uv - 0.5, d) / max(abs(d.x) + abs(d.y), 0.0001) + 0.5, 0.0, 1.0);
    return mix(n_n1_image(uv), k_n2_b, t);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
