#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    float a = radians(135.0);
    vec2 d = vec2(cos(a), sin(a)) * 1.5 / u_resolution;
    float e = (luma(n_n1_image(uv + d)) - luma(n_n1_image(uv - d))) * 2.0;
    return vec4(clamp(vec3(0.5 + e), 0.0, 1.0), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
