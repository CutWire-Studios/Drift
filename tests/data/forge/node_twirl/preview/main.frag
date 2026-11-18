#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform vec2 k_n2_center;
uniform float k_n2_radius;
uniform float k_n2_angle;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 ctr = k_n2_center;
    vec2 d = (uv - ctr) * vec2(asp, 1.0);
    float r = length(d);
    float fall = 1.0 - smoothstep(0.0, max(k_n2_radius, 0.0001), r);
    float a = atan(d.y, d.x) + k_n2_angle * fall * fall;
    vec2 nd = r > 1e-6 ? vec2(cos(a), sin(a)) * r : d;
    vec2 p = ctr + vec2(nd.x / asp, nd.y);
    return n_n1_image(clamp(p, 0.0, 1.0));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
