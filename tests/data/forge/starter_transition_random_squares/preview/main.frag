#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform float u_progress;
uniform vec2 k_n3_count;
uniform float k_n3_seed;
uniform float k_n4_softness;

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return texture(u_texture1, uv);
}

float n_n3_random(vec2 uv) {
    vec2 n = max(floor(k_n3_count), vec2(1.0));
    vec2 id = floor(uv * n);
    return hash21(id + k_n3_seed * 1.31);
}

float n_n4_mask(vec2 uv) {
    float x = clamp(n_n3_random(uv), 0.0, 1.0);
    
    float s = max(k_n4_softness, 0.0001);
    float front = mix(-s, 1.0 + s, u_progress);
    return 1.0 - smoothstep(front - s, front + s, x);
}

vec4 n_n5_image(vec2 uv) {
    return mix(n_n1_image(uv), n_n2_image(uv), clamp(n_n4_mask(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n5_image(v_texCoord);
}
