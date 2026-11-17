#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;
uniform float k_n2_pattern;
uniform float k_n2_softness;

float n_n2_mask(vec2 uv) {
    float x = clamp(k_n2_pattern, 0.0, 1.0);
    
    float s = max(k_n2_softness, 0.0001);
    float front = mix(-s, 1.0 + s, u_time);
    return 1.0 - smoothstep(front - s, front + s, x);
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
