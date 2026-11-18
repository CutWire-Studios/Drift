#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float music_level;
uniform float music_bass;
uniform float music_beat;
uniform sampler2D other;
uniform sampler2D matte;
uniform sampler2D music;
uniform float k_n4_x;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return texture(other, uv);
}

float n_n3_mask(vec2 uv) {
    return texture(matte, uv).r;
}

vec4 n_n5_image(vec2 uv) {
    return mix(n_n1_image(uv), n_n2_image(uv), clamp(n_n3_mask(uv), 0.0, 1.0));
}

float n_n4_level(vec2 uv) {
    return music_level;
}

vec4 n_n6_image(vec2 uv) {
    return mix(n_n5_image(uv), n_n1_image(uv), clamp(n_n4_level(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n6_image(v_texCoord);
}
