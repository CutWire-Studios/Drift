#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float u_progress;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return texture(u_texture1, uv);
}

float n_n3_mask(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 d = (uv - vec2(0.5, 0.5)) * vec2(asp, 1.0);
    float a = fract((atan(d.x, -d.y) - radians(0.0)) / 6.2831853);
    
    float s = max(0.01, 0.0001);
    float front = mix(-s, 1.0 + s, u_progress);
    return 1.0 - smoothstep(front - s, front + s, a);
}

vec4 n_n4_image(vec2 uv) {
    return mix(n_n1_image(uv), n_n2_image(uv), clamp(n_n3_mask(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n4_image(v_texCoord);
}
