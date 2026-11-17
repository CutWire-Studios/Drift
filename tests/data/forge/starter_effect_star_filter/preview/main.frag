#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float spikes;
uniform float spikeLength;
float pv_spikes() { return spikes; }
float pv_spikeLength() { return spikeLength; }
uniform float k_n2_threshold;
uniform float k_n2_rotation;
uniform float k_n2_intensity;
uniform vec4 k_n2_tint;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 sum = vec3(0.0);
    float n = clamp(floor(pv_spikes()), 1.0, 8.0);
    float thr = k_n2_threshold;
    for (int i = 0; i < 8; ++i) {
        if (float(i) >= n) break;
        float a = radians(k_n2_rotation) + float(i) * 3.14159265 / n;
        vec2 dir = vec2(cos(a), sin(a)) * pv_spikeLength() / u_resolution;
        for (int k = 1; k <= 12; ++k) {
            float t = float(k) / 12.0;
            float w = (1.0 - t) * (1.0 - t);
            sum += (max(n_n1_image(uv + dir * t).rgb - thr, 0.0) + max(n_n1_image(uv - dir * t).rgb - thr, 0.0)) * w;
        }
    }
    sum /= n * 4.0 * max(1.0 - thr, 0.05);
    return vec4(clamp(col.rgb + sum * k_n2_intensity * k_n2_tint.rgb, 0.0, 1.0), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
