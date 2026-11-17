#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform vec2 k_n2_center;
uniform float k_n2_frequency;
uniform float k_n2_speed;
uniform float k_n2_amplitude;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 d = (uv - k_n2_center) * vec2(asp, 1.0);
    float r = length(d);
    vec2 dir = r > 1e-5 ? d / r : vec2(0.0);
    float w = sin(r * k_n2_frequency - u_time * k_n2_speed) * k_n2_amplitude;
    vec2 p = uv + vec2(dir.x / asp, dir.y) * w;
    return n_n1_image(clamp(p, 0.0, 1.0));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
