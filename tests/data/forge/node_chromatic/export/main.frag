#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float a = radians(0.0);
    vec2 delta = vec2(cos(a), sin(a)) * 8.0 / u_resolution;
    vec4 g = n_n1_image(uv);
    float r = n_n1_image(clamp(uv + delta, 0.0, 1.0)).r;
    float b = n_n1_image(clamp(uv - delta, 0.0, 1.0)).b;
    return vec4(r, g.g, b, g.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
