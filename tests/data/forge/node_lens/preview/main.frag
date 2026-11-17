#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform vec2 k_n2_center;
uniform float k_n2_area;
uniform float k_n2_amount;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 ctr = k_n2_center;
    vec2 d = (uv - ctr) * vec2(asp, 1.0);
    float ar = max(k_n2_area, 0.0001);
    float inside = 1.0 - smoothstep(ar * 0.8, ar, length(d));
    float k = min(ar, 1.0);
    vec2 dn = d / k;
    d = mix(d, dn * (1.0 - k_n2_amount * 0.5 + k_n2_amount * dot(dn, dn)) * k, inside);
    vec2 p = ctr + vec2(d.x / asp, d.y);
    return n_n1_image(clamp(p, 0.0, 1.0));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
