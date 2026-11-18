#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_progress;
uniform float k_n2_angle;
uniform float k_n2_softness;

float n_n2_mask(vec2 uv) {
    float ang = radians(k_n2_angle);
    vec2 d = vec2(cos(ang), sin(ang));
    float x = dot(uv - 0.5, d) / max(abs(d.x) + abs(d.y), 0.0001) + 0.5;
    float s = max(k_n2_softness, 0.0001);
    float front = mix(-s, 1.0 + s, u_progress);
    return 1.0 - smoothstep(front - s, front + s, x);
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
