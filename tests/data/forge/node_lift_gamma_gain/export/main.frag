#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 lift = (vec4(0.45, 0.5, 0.56, 1.0).rgb - 0.5) * 0.5;
    vec3 gain = 1.0 + (vec4(0.56, 0.52, 0.46, 1.0).rgb - 0.5);
    vec3 gam = exp2(-(vec4(0.5, 0.5, 0.5, 1.0).rgb - 0.5) * 2.0);
    vec3 rgb = col.rgb * gain;
    rgb = rgb + lift * (1.0 - rgb);
    rgb = pow(clamp(rgb, 0.0, 1.0), gam);
    return vec4(mix(col.rgb, rgb, 1.0), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
