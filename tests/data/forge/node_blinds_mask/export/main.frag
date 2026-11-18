#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_progress;

float n_n2_mask(vec2 uv) {
    float ang = radians(0.0);
    float x = fract(dot(uv, vec2(cos(ang), sin(ang))) * 10.0);
    float s = max(0.05, 0.0001);
    float front = mix(-s, 1.0 + s, u_progress);
    return 1.0 - smoothstep(front - s, front + s, x);
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
