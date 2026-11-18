#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec2 cell = floor(uv * u_resolution / max(1.5, 1.0));
    float n = hash21(cell + floor(u_time * 24.0) * 17.31 + 0.0 * 3.17) - 0.5;
    return vec4(clamp(col.rgb + n * 0.12, 0.0, 1.0), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
