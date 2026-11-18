#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float amount;
uniform float flip;
uniform vec2 centre;
uniform float slices;
uniform float shuffle;
uniform vec4 tint;
uniform vec3 plain;
uniform vec4 area;
uniform sampler2D picture;
uniform sampler2D ramp;
uniform sampler2D motion;
uniform float style;
float pv_amount() { return amount; }
float pv_flip() { return flip; }
vec2 pv_centre() { return centre; }
float pv_slices() { return slices; }
float pv_shuffle() { return shuffle; }
vec4 pv_tint() { return tint; }
vec3 pv_plain() { return plain; }
vec4 pv_area() { return area; }
float pv_style() { return style; }

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 ctr = pv_centre();
    vec2 d = (uv - ctr) * vec2(asp, 1.0);
    float seg = 6.2832 / max(floor(pv_slices()), 2.0);
    float a = atan(d.y, d.x) + radians(0.0);
    a = mod(a, seg);
    a = abs(a - seg * 0.5);
    vec2 nd = vec2(cos(a), sin(a)) * length(d);
    vec2 p = ctr + vec2(nd.x / asp, nd.y);
    return n_n1_image(1.0 - abs(mod(p, 2.0) - 1.0));
}

vec4 n_n3_image(vec2 uv) {
    vec4 col = n_n2_image(uv);
    vec3 tinted = luma(col) * pv_tint().rgb * 1.6;
    return vec4(mix(col.rgb, clamp(tinted, 0.0, 1.0), pv_amount()), col.a);
}

float n_n6_value(vec2 uv) {
    float t = fract(u_time / max(2.0, 0.0001));
    float y;
    y = texture(motion, vec2(t * (255.0 / 256.0) + 0.5 / 256.0, 0.5)).r;
    return mix(0.0, 1.0, y);
}

vec4 n_n5_image(vec2 uv) {
    vec4 col = n_n3_image(uv);
    float t = clamp(luma(col), 0.0, 1.0);
    vec4 g = texture(ramp, vec2(t * (255.0 / 256.0) + 0.5 / 256.0, 0.5));
    return vec4(mix(col.rgb, g.rgb, n_n6_value(uv) * g.a), col.a);
}

vec4 n_n8_image(vec2 uv) {
    vec4 col = n_n5_image(uv);
    vec2 cell = floor(uv * u_resolution / max(1.5, 1.0));
    float n = hash21(cell + floor(u_time * 24.0) * 17.31 + pv_shuffle() * 3.17) - 0.5;
    return vec4(clamp(col.rgb + n * 0.12, 0.0, 1.0), col.a);
}

vec4 n_n4_image(vec2 uv) {
    return texture(picture, uv * 1.0);
}

vec4 n_n9_image(vec2 uv) {
    float w = floor(pv_style() + 0.5);
    if (w < 0.5) return n_n8_image(uv);
    if (w < 1.5) return n_n4_image(uv);
    if (w < 2.5) return vec4(0.0, 0.0, 0.0, 1.0);
    return vec4(0.0, 0.0, 0.0, 1.0);
}

vec4 n_n10_color(vec2 uv) {
    return vec4(pv_plain(), 1.0);
}

float n_n7_mask(vec2 uv) {
    vec4 rg = pv_area();
    vec2 half2 = max(rg.zw * 0.5, vec2(0.0001));
    vec2 ctr = rg.xy + half2;
    float sd = (length((uv - ctr) / half2) - 1.0) * min(half2.x, half2.y);
    float soft = max(0.02, 0.0005);
    float m = 1.0 - smoothstep(-soft, soft, sd);
    return m;
}

vec4 n_n12_image(vec2 uv) {
    return mix(n_n9_image(uv), n_n10_color(uv), clamp(n_n7_mask(uv), 0.0, 1.0));
}

float n_n11_value(vec2 uv) {
    return pv_flip() > 0.5 ? 1.0 : 0.0;
}

vec4 n_n13_image(vec2 uv) {
    return mix(n_n12_image(uv), n_n1_image(uv), clamp(n_n11_value(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n13_image(v_texCoord);
}
