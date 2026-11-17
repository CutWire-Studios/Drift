#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float k_n2_seed;
uniform float k_n2_weave;
uniform float k_n2_flicker;
uniform float k_n2_scratches;
uniform float k_n2_dust;

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float frame = floor(u_time * 18.0) + k_n2_seed * 7.0;
    vec2 jitter = (vec2(hash11(frame), hash11(frame + 3.3)) - 0.5) * 0.006 * k_n2_weave;
    vec4 col = n_n1_image(clamp(uv + jitter, 0.0, 1.0));
    float fl = 1.0 + (hash11(frame + 9.1) - 0.5) * 0.3 * k_n2_flicker;
    vec3 rgb = col.rgb * fl;
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        float on = step(1.0 - k_n2_scratches * 0.6, hash11(frame * 1.7 + fi * 11.0));
        float x = hash11(frame + fi * 5.7);
        float line = 1.0 - smoothstep(0.0, 0.0015, abs(uv.x - x - sin(uv.y * 9.0 + fi) * 0.003));
        rgb = mix(rgb, vec3(0.9), line * on * 0.7);
    }
    vec2 cell = floor(uv * vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0) * 60.0);
    float speck = step(1.0 - k_n2_dust * 0.02, hash21(cell + frame * 13.0));
    rgb = mix(rgb, vec3(0.05), speck * 0.85);
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
