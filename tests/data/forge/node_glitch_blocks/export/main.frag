#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
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
    float tick = floor(u_time * 12.0) + 0.0 * 31.7;
    float row = floor(uv.y * 24.0);
    float h = hash21(vec2(row, tick));
    float on = step(1.0 - 0.5 * 0.6, h);
    float shift = (hash21(vec2(tick, row * 1.7)) - 0.5) * 0.25 * 0.5 * on;
    vec2 p = vec2(fract(uv.x + shift), uv.y);
    vec4 g = n_n1_image(p);
    float r = n_n1_image(vec2(fract(p.x + shift * 0.3), p.y)).r;
    float b = n_n1_image(vec2(fract(p.x - shift * 0.3), p.y)).b;
    return vec4(r, g.g, b, g.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
