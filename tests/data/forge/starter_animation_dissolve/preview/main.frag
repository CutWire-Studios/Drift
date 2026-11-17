#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float u_direction;
uniform float scale;
uniform float softness;
float pv_scale() { return scale; }
float pv_softness() { return softness; }
uniform float k_n2_speed;
uniform float k_n2_seed;

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

float fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += amp * valueNoise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return v;
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

float n_n2_value(vec2 uv) {
    return fbm(uv * vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0) * pv_scale() + vec2(mix(u_progress, 1.0 - u_progress, u_direction) * k_n2_speed, mix(u_progress, 1.0 - u_progress, u_direction) * k_n2_speed * 0.7) + vec2(k_n2_seed * 17.31, k_n2_seed * 7.77));
}

float n_n3_mask(vec2 uv) {
    float x = clamp(n_n2_value(uv), 0.0, 1.0);
    
    float s = max(pv_softness(), 0.0001);
    float front = mix(-s, 1.0 + s, mix(u_progress, 1.0 - u_progress, u_direction));
    return 1.0 - smoothstep(front - s, front + s, x);
}

vec4 n_n4_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    return vec4(col.rgb, col.a * clamp(n_n3_mask(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n4_image(v_texCoord);
}
