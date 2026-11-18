#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform float u_progress;
uniform float direction;
uniform float softness;
uniform vec3 glowColor;
float pv_direction() { return direction; }
float pv_softness() { return softness; }
vec3 pv_glowColor() { return glowColor; }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return texture(u_texture1, uv);
}

float n_n3_mask(vec2 uv) {
    float ang = radians(pv_direction());
    vec2 d = vec2(cos(ang), sin(ang));
    float x = dot(uv - 0.5, d) / max(abs(d.x) + abs(d.y), 0.0001) + 0.5;
    float s = max(pv_softness(), 0.0001);
    float front = mix(-s, 1.0 + s, u_progress);
    return 1.0 - smoothstep(front - s, front + s, x);
}

vec4 n_n4_image(vec2 uv) {
    return mix(n_n1_image(uv), n_n2_image(uv), clamp(n_n3_mask(uv), 0.0, 1.0));
}

vec4 n_n5_image(vec2 uv) {
    float m = clamp(n_n3_mask(uv), 0.0, 1.0);
    float band = pow(1.0 - abs(m * 2.0 - 1.0), 2.0);
    return vec4(vec4(pv_glowColor(), 1.0).rgb * band * 1.5, band);
}

vec4 n_n6_image(vec2 uv) {
    vec4 b = n_n4_image(uv);
    vec4 l = n_n5_image(uv);
    vec3 rgb = min(b.rgb + l.rgb, 1.0);
    return vec4(mix(b.rgb, rgb, clamp(1.0 * l.a, 0.0, 1.0)), b.a);
}

void main() {
    fragColor = n_n6_image(v_texCoord);
}
