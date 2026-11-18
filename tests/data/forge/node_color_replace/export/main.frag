#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    float dist = distance(col.rgb, vec4(0.85, 0.15, 0.15, 1.0).rgb);
    float m = 1.0 - smoothstep(0.25, 0.25 + 0.1 + 0.0001, dist);
    vec3 moved = clamp(col.rgb - vec4(0.85, 0.15, 0.15, 1.0).rgb + vec4(0.15, 0.45, 0.95, 1.0).rgb, 0.0, 1.0);
    return vec4(mix(col.rgb, moved, m), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
