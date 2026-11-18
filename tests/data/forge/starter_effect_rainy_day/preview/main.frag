#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float rainAmount;
float pv_rainAmount() { return rainAmount; }
uniform float k_n2_amount;
uniform float k_n3_angle;
uniform float k_n3_seed;
uniform float k_n3_speed;
uniform float k_n3_length;
uniform float k_n4_opacity;

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

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    float t = k_n2_amount;
    vec3 rgb = col.rgb * vec3(1.0 + 0.25 * t, 1.0 + 0.05 * t, 1.0 - 0.25 * t);
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}

float n_n3_mask(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 q = rot2(radians(k_n3_angle)) * ((uv - 0.5) * vec2(asp, 1.0));
    float acc = 0.0;
    for (int l = 0; l < 3; ++l) {
        float fl = float(l);
        float cols = 80.0 + fl * 60.0;
        float x = q.x * cols;
        float col = floor(x);
        float h = hash11(col * 1.37 + fl * 91.7 + k_n3_seed);
        if (h > pv_rainAmount()) continue;
        float y = q.y - u_time * k_n3_speed * (1.2 + fl * 0.5 + h) + hash11(col + 5.1) * 7.0;
        float seg = fract(y / (k_n3_length * (3.0 + fl)));
        float streak = smoothstep(0.0, 0.2, seg) * (1.0 - smoothstep(0.25, 0.3, seg));
        float thin = 1.0 - smoothstep(0.05, 0.25, abs(fract(x) - 0.5));
        acc += streak * thin * (0.5 + fl * 0.25);
    }
    return clamp(acc, 0.0, 1.0);
}

vec4 n_n4_image(vec2 uv) {
    vec4 b = n_n2_image(uv);
    vec4 l = vec4(vec3(n_n3_mask(uv)), 1.0);
    vec3 rgb = 1.0 - (1.0 - b.rgb) * (1.0 - l.rgb);
    return vec4(mix(b.rgb, rgb, clamp(k_n4_opacity * l.a, 0.0, 1.0)), b.a);
}

void main() {
    fragColor = n_n4_image(v_texCoord);
}
