#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 hsv = rgb2hsv(col.rgb);
    float dh = abs(fract(hsv.x - 0.0 / 360.0 + 0.5) - 0.5) * 360.0;
    float m = (1.0 - smoothstep(30.0, 30.0 + 20.0 + 0.001, dh)) * smoothstep(0.05, 0.2, hsv.y);
    vec3 adj = vec3(fract(hsv.x + 0.0 / 360.0), clamp(hsv.y * (1.0 + 0.0), 0.0, 1.0), clamp(hsv.z * (1.0 + 0.0), 0.0, 1.0));
    vec3 rest = mix(col.rgb, vec3(dot(col.rgb, vec3(0.2126, 0.7152, 0.0722))), 0.0);
    return vec4(mix(rest, hsv2rgb(adj), m), col.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
