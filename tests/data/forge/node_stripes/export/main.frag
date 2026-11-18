#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;

float n_n2_mask(vec2 uv) {
    float ang = radians(0.0);
    float x = dot(uv - 0.5, vec2(cos(ang), sin(ang))) * 10.0 + u_time * 0.0;
    float f = abs(fract(x) - 0.5) * 2.0;
    float soft = max(0.02, 0.0005);
    return 1.0 - smoothstep(0.5 - soft, 0.5 + soft, f);
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
