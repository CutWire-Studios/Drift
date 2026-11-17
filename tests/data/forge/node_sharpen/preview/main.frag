#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float k_n2_amount;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec2 px = 1.0 / u_resolution;
    vec4 col = n_n1_image(uv);
    vec4 n = n_n1_image(uv + vec2(px.x, 0.0)) + n_n1_image(uv - vec2(px.x, 0.0))
           + n_n1_image(uv + vec2(0.0, px.y)) + n_n1_image(uv - vec2(0.0, px.y));
    vec3 rgb = col.rgb + (col.rgb * 4.0 - n.rgb) * k_n2_amount;
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
