#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 k_n2_count;
uniform float k_n2_seed;

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float n_n2_random(vec2 uv) {
    vec2 n = max(floor(k_n2_count), vec2(1.0));
    vec2 id = floor(uv * n);
    return hash21(id + k_n2_seed * 1.31);
}

void main() {
    fragColor = vec4(vec3(n_n2_random(v_texCoord)), 1.0);
}
