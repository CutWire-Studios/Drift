#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float k_n2_speed;
uniform float k_n2_seed;
uniform float k_n2_amount;
uniform float k_n2_size;

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float n_n2_mask(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 base = uv * vec2(asp, 1.0);
    float acc = 0.0;
    for (int l = 0; l < 4; ++l) {
        float fl = float(l);
        float scale = 6.0 + fl * 5.0;
        vec2 p = base * scale;
        p.y -= u_time * k_n2_speed * scale * (0.6 + fl * 0.15);
        p.x += sin(p.y * 0.4 + fl * 3.0 + u_time) * 0.4;
        vec2 cell = floor(p);
        vec2 h = hash22(cell + fl * 17.0 + k_n2_seed);
        if (h.x > k_n2_amount) continue;
        vec2 ctr = cell + 0.2 + h * 0.6;
        float r = (0.04 + h.y * 0.06) * k_n2_size * (1.0 + fl * 0.3);
        acc += 1.0 - smoothstep(r * 0.4, r, length(p - ctr));
    }
    return clamp(acc, 0.0, 1.0);
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
