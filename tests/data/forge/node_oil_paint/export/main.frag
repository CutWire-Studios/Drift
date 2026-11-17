#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float r = clamp(4.0, 1.0, 6.0);
    vec3 m0 = vec3(0.0); vec3 m1 = vec3(0.0); vec3 m2 = vec3(0.0); vec3 m3 = vec3(0.0);
    vec3 q0 = vec3(0.0); vec3 q1 = vec3(0.0); vec3 q2 = vec3(0.0); vec3 q3 = vec3(0.0);
    float n0 = 0.0; float n1 = 0.0; float n2 = 0.0; float n3 = 0.0;
    for (int j = -6; j <= 6; ++j) {
        for (int i = -6; i <= 6; ++i) {
            if (abs(float(i)) > r || abs(float(j)) > r) continue;
            vec3 s = n_n1_image(uv + vec2(float(i), float(j)) / u_resolution).rgb;
            if (i <= 0 && j <= 0) { m0 += s; q0 += s * s; n0 += 1.0; }
            if (i >= 0 && j <= 0) { m1 += s; q1 += s * s; n1 += 1.0; }
            if (i <= 0 && j >= 0) { m2 += s; q2 += s * s; n2 += 1.0; }
            if (i >= 0 && j >= 0) { m3 += s; q3 += s * s; n3 += 1.0; }
        }
    }
    m0 /= n0; m1 /= n1; m2 /= n2; m3 /= n3;
    vec3 v0 = q0 / n0 - m0 * m0; vec3 v1 = q1 / n1 - m1 * m1;
    vec3 v2 = q2 / n2 - m2 * m2; vec3 v3 = q3 / n3 - m3 * m3;
    float s0 = v0.r + v0.g + v0.b; float s1 = v1.r + v1.g + v1.b;
    float s2 = v2.r + v2.g + v2.b; float s3 = v3.r + v3.g + v3.b;
    vec3 best = m0; float bv = s0;
    if (s1 < bv) { best = m1; bv = s1; }
    if (s2 < bv) { best = m2; bv = s2; }
    if (s3 < bv) { best = m3; }
    return vec4(best, n_n1_image(uv).a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
