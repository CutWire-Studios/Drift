#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec2 d = (uv - 0.5) * vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0);
    float v = smoothstep(0.75 - 0.45, 0.75, length(d) * 1.2);
    return vec4(mix(col.rgb, vec4(0.0, 0.0, 0.0, 1.0).rgb, v * 0.6), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
