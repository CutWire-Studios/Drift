#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

float n_n2_mask(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 q = rot2(radians(10.0)) * ((uv - 0.5) * vec2(asp, 1.0));
    float acc = 0.0;
    for (int l = 0; l < 3; ++l) {
        float fl = float(l);
        float cols = 80.0 + fl * 60.0;
        float x = q.x * cols;
        float col = floor(x);
        float h = hash11(col * 1.37 + fl * 91.7 + 0.0);
        if (h > 0.5) continue;
        float y = q.y - u_time * 1.5 * (1.2 + fl * 0.5 + h) + hash11(col + 5.1) * 7.0;
        float seg = fract(y / (0.08 * (3.0 + fl)));
        float streak = smoothstep(0.0, 0.2, seg) * (1.0 - smoothstep(0.25, 0.3, seg));
        float thin = 1.0 - smoothstep(0.05, 0.25, abs(fract(x) - 0.5));
        acc += streak * thin * (0.5 + fl * 0.25);
    }
    return clamp(acc, 0.0, 1.0);
}

void main() {
    fragColor = vec4(vec3(n_n2_mask(v_texCoord)), 1.0);
}
