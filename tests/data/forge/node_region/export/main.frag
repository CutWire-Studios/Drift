#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

float n_n2_mask(vec2 uv) {
    vec4 rg = vec4(0.3, 0.25, 0.4, 0.5);
    vec2 half2 = max(rg.zw * 0.5, vec2(0.0001));
    vec2 ctr = rg.xy + half2;
    float sd = (length((uv - ctr) / half2) - 1.0) * min(half2.x, half2.y);
    float soft = max(0.02, 0.0005);
    float m = 1.0 - smoothstep(-soft, soft, sd);
    return m;
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
