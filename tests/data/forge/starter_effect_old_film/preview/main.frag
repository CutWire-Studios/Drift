#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float damage;
float pv_damage() { return damage; }
uniform float k_n3_seed;
uniform float k_n3_weave;
uniform float k_n2_amount;
uniform float k_n3_flicker;
uniform float k_n3_dust;
uniform float k_n4_size;
uniform float k_n4_seed;
uniform float k_n4_amount;
uniform float k_n5_radius;
uniform float k_n5_softness;
uniform vec4 k_n5_color;
uniform float k_n5_amount;

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 rgb = mix(vec3(luma(col)), col.rgb, k_n2_amount);
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}

vec4 n_n3_image(vec2 uv) {
    float frame = floor(u_time * 18.0) + k_n3_seed * 7.0;
    vec2 jitter = (vec2(hash11(frame), hash11(frame + 3.3)) - 0.5) * 0.006 * k_n3_weave;
    vec4 col = n_n2_image(clamp(uv + jitter, 0.0, 1.0));
    float fl = 1.0 + (hash11(frame + 9.1) - 0.5) * 0.3 * k_n3_flicker;
    vec3 rgb = col.rgb * fl;
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        float on = step(1.0 - pv_damage() * 0.6, hash11(frame * 1.7 + fi * 11.0));
        float x = hash11(frame + fi * 5.7);
        float line = 1.0 - smoothstep(0.0, 0.0015, abs(uv.x - x - sin(uv.y * 9.0 + fi) * 0.003));
        rgb = mix(rgb, vec3(0.9), line * on * 0.7);
    }
    vec2 cell = floor(uv * vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0) * 60.0);
    float speck = step(1.0 - k_n3_dust * 0.02, hash21(cell + frame * 13.0));
    rgb = mix(rgb, vec3(0.05), speck * 0.85);
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}

vec4 n_n4_image(vec2 uv) {
    vec4 col = n_n3_image(uv);
    vec2 cell = floor(uv * u_resolution / max(k_n4_size, 1.0));
    float n = hash21(cell + floor(u_time * 24.0) * 17.31 + k_n4_seed * 3.17) - 0.5;
    return vec4(clamp(col.rgb + n * k_n4_amount, 0.0, 1.0), col.a);
}

vec4 n_n5_image(vec2 uv) {
    vec4 col = n_n4_image(uv);
    vec2 d = (uv - 0.5) * vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0);
    float v = smoothstep(k_n5_radius - k_n5_softness, k_n5_radius, length(d) * 1.2);
    return vec4(mix(col.rgb, k_n5_color.rgb, v * k_n5_amount), col.a);
}

void main() {
    fragColor = n_n5_image(v_texCoord);
}
