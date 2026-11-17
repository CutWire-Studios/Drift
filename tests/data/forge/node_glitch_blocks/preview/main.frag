#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;
uniform float k_n2_rate;
uniform float k_n2_seed;
uniform float k_n2_blocks;
uniform float k_n2_amount;

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float tick = floor(u_time * k_n2_rate) + k_n2_seed * 31.7;
    float row = floor(uv.y * k_n2_blocks);
    float h = hash21(vec2(row, tick));
    float on = step(1.0 - k_n2_amount * 0.6, h);
    float shift = (hash21(vec2(tick, row * 1.7)) - 0.5) * 0.25 * k_n2_amount * on;
    vec2 p = vec2(fract(uv.x + shift), uv.y);
    vec4 g = n_n1_image(p);
    float r = n_n1_image(vec2(fract(p.x + shift * 0.3), p.y)).r;
    float b = n_n1_image(vec2(fract(p.x - shift * 0.3), p.y)).b;
    return vec4(r, g.g, b, g.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
