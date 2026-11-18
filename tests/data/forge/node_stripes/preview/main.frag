#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;
uniform float k_n2_angle;
uniform float k_n2_count;
uniform float k_n2_speed;
uniform float k_n2_softness;
uniform float k_n2_width;

float n_n2_mask(vec2 uv) {
    float ang = radians(k_n2_angle);
    float x = dot(uv - 0.5, vec2(cos(ang), sin(ang))) * k_n2_count + u_time * k_n2_speed;
    float f = abs(fract(x) - 0.5) * 2.0;
    float soft = max(k_n2_softness, 0.0005);
    return 1.0 - smoothstep(k_n2_width - soft, k_n2_width + soft, f);
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
