#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 rgb = clamp((col.rgb - 0.0) / max(1.0 - 0.0, 0.0001), 0.0, 1.0);
    rgb = pow(rgb, vec3(1.0 / max(1.0, 0.0001)));
    return vec4(mix(vec3(0.0), vec3(1.0), rgb), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
