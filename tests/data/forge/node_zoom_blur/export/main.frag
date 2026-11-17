#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec2 ctr = vec2(0.5, 0.5);
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 24; ++k) {
        float s = 1.0 - 0.15 * float(k) / 23.0;
        sum += n_n1_image(ctr + (uv - ctr) * s);
    }
    return sum / 24.0;
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
