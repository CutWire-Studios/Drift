#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec2 px = uv * u_resolution;
    mat2 m = rot2(radians(45.0));
    vec2 q = m * px / max(10.0, 1.0);
    vec2 cellCenter = (floor(q) + 0.5);
    vec2 sampleUv = (transpose(m) * (cellCenter * max(10.0, 1.0))) / u_resolution;
    vec4 src = n_n1_image(clamp(sampleUv, 0.0, 1.0));
    float r = sqrt(1.0 - luma(src)) * 0.75;
    float d = length(q - cellCenter);
    float dotMask = 1.0 - smoothstep(r - 0.08, r + 0.08, d);
    vec3 ink = vec4(0.08, 0.08, 0.1, 1.0).rgb;
    return vec4(mix(vec4(0.98, 0.96, 0.9, 1.0).rgb, ink, dotMask), n_n1_image(uv).a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
