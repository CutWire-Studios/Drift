#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_progress;
uniform vec2 k_n2_center;
uniform float k_n2_start;
uniform float k_n2_softness;

float n_n2_mask(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 d = (uv - k_n2_center) * vec2(asp, 1.0);
    float a = fract((atan(d.x, -d.y) - radians(k_n2_start)) / 6.2831853);
    
    float s = max(k_n2_softness, 0.0001);
    float front = mix(-s, 1.0 + s, u_progress);
    return 1.0 - smoothstep(front - s, front + s, a);
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
