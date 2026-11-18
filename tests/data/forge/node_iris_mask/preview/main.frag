#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_progress;
uniform vec2 k_n2_center;
uniform float k_n2_softness;

float n_n2_mask(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 ctr = k_n2_center;
    vec2 far = max(ctr, 1.0 - ctr) * vec2(asp, 1.0);
    float x = length((uv - ctr) * vec2(asp, 1.0)) / max(length(far), 0.0001);
    float s = max(k_n2_softness, 0.0001);
    float front = mix(-s, 1.0 + s, u_progress);
    return 1.0 - smoothstep(front - s, front + s, x);
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
