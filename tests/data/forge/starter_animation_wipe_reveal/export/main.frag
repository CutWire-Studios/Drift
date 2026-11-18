#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_progress;
uniform float u_direction;
uniform float angle;
uniform float softness;
float pv_angle() { return angle; }
float pv_softness() { return softness; }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

float n_n2_mask(vec2 uv) {
    float ang = radians(pv_angle());
    vec2 d = vec2(cos(ang), sin(ang));
    float x = dot(uv - 0.5, d) / max(abs(d.x) + abs(d.y), 0.0001) + 0.5;
    float s = max(pv_softness(), 0.0001);
    float front = mix(-s, 1.0 + s, mix(u_progress, 1.0 - u_progress, u_direction));
    return 1.0 - smoothstep(front - s, front + s, x);
}

vec4 n_n3_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    return vec4(col.rgb, col.a * clamp(n_n2_mask(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n3_image(v_texCoord);
}
