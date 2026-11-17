#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float k_n2_amount;

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float a = k_n2_amount;
    float t = u_time;
    float wob = (valueNoise(vec2(uv.y * 8.0, t * 4.0)) - 0.5) * 0.01 * a;
    float band = smoothstep(0.96, 1.0, sin(uv.y * 3.0 - t * 1.7) * 0.5 + 0.5);
    vec2 p = vec2(uv.x + wob + band * 0.02 * a, uv.y);
    vec4 g = n_n1_image(clamp(p, 0.0, 1.0));
    float r = n_n1_image(clamp(p + vec2(0.004 * a, 0.0), 0.0, 1.0)).r;
    float b = n_n1_image(clamp(p - vec2(0.004 * a, 0.0), 0.0, 1.0)).b;
    vec3 col = vec3(r, g.g, b);
    float n = hash21(floor(uv * u_resolution * 0.5) + floor(t * 30.0)) - 0.5;
    col += n * 0.08 * a + band * 0.15 * a;
    return vec4(clamp(col, 0.0, 1.0), g.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
