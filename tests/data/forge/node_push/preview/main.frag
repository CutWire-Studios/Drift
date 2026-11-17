#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform float u_progress;

vec4 n_n4_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_texture1, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec2 d = vec2(-1.0, 0.0);
    float p = clamp(u_progress, 0.0, 1.0);
    vec2 pt = uv - d * (p - 1.0);
    if (pt.x >= 0.0 && pt.x <= 1.0 && pt.y >= 0.0 && pt.y <= 1.0) return n_n4_image(pt);
    vec2 pf = uv - d * p;
    return n_n1_image(clamp(pf, 0.0, 1.0));
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
