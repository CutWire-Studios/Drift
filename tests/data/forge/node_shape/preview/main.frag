#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float k_n2_size;
uniform float k_n2_rotation;
uniform vec2 k_n2_center;
uniform float k_n2_softness;

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

float n_n2_mask(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 d = rot2(-radians(k_n2_rotation)) * ((uv - k_n2_center) * vec2(asp, 1.0));
    float sd = length(d) - k_n2_size;
    float soft = max(k_n2_softness, 0.0005);
    return 1.0 - smoothstep(-soft, soft, sd);
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
