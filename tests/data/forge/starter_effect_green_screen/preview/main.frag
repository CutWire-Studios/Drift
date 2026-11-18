#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float tolerance;
float pv_tolerance() { return tolerance; }
uniform vec4 k_n2_key;
uniform float k_n2_softness;
uniform float k_n2_spill;

vec3 rgb2ycc(vec3 c) {
    return vec3(
        dot(c, vec3(0.299, 0.587, 0.114)),
        dot(c, vec3(-0.168736, -0.331264, 0.5)) + 0.5,
        dot(c, vec3(0.5, -0.418688, -0.081312)) + 0.5);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 k = rgb2ycc(k_n2_key.rgb);
    vec3 y = rgb2ycc(col.rgb);
    float dist = distance(y.yz, k.yz);
    float m = smoothstep(pv_tolerance(), pv_tolerance() + k_n2_softness + 0.0001, dist);
    vec3 key = k_n2_key.rgb;
    vec3 rgb = col.rgb;
    float dom = key.g >= max(key.r, key.b) ? rgb.g - max(rgb.r, rgb.b) : key.b >= key.r ? rgb.b - max(rgb.r, rgb.g) : rgb.r - max(rgb.g, rgb.b);
    vec3 spillDir = key / max(max(key.r, max(key.g, key.b)), 0.0001);
    rgb -= spillDir * max(dom, 0.0) * k_n2_spill;
    return vec4(clamp(rgb, 0.0, 1.0), col.a * m);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
