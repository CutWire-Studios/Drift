#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;
uniform float k_n2_speed;
uniform float k_n2_count;
uniform float k_n2_amount;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    float l = 0.5 + 0.5 * sin((uv.y + u_time * k_n2_speed * 0.05) * k_n2_count * 3.14159);
    return vec4(col.rgb * (1.0 - k_n2_amount * (1.0 - l)), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
