#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec4 k_n2_from;
uniform float k_n2_tolerance;
uniform float k_n2_softness;
uniform vec4 k_n2_to;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    float dist = distance(col.rgb, k_n2_from.rgb);
    float m = 1.0 - smoothstep(k_n2_tolerance, k_n2_tolerance + k_n2_softness + 0.0001, dist);
    vec3 moved = clamp(col.rgb - k_n2_from.rgb + k_n2_to.rgb, 0.0, 1.0);
    return vec4(mix(col.rgb, moved, m), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
