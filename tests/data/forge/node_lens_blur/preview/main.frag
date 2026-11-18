#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float k_n2_radius;
uniform float k_n2_boost;

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float r = max(k_n2_radius, 0.0);
    vec4 acc = vec4(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 64; ++i) {
        float fi = float(i);
        float rr = sqrt((fi + 0.5) / 64.0) * r;
        float a = fi * 2.39996323;
        vec4 s = n_n1_image(uv + vec2(cos(a), sin(a)) * rr / u_resolution);
        float w = 1.0 + k_n2_boost * pow(luma(s), 4.0) * 8.0;
        acc += s * w;
        wsum += w;
    }
    return acc / wsum;
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
