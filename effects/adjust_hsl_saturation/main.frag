#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float red, orange, yellow, green, aqua, blue, purple, magenta;

// The eight Lightroom-style band centres, with the reach of each band's falloff to
// either side. Asymmetric on purpose: the centres are not evenly spaced, and one
// shared width lets the tightly packed warm bands drown each other out — with 40°
// all round, pulling Orange asks Yellow's opinion more than Orange's. Each reach is
// the distance to the neighbouring centre instead, so a band is at full strength on
// its own hue, at zero on its neighbours', and the eight weights sum to 1 everywhere.
const float BAND_H[8]  = float[8](  0.0,  32.0,  60.0, 120.0, 180.0, 240.0, 280.0, 320.0);
const float BAND_LO[8] = float[8]( 40.0,  32.0,  28.0,  60.0,  60.0,  60.0,  40.0,  40.0);
const float BAND_HI[8] = float[8]( 32.0,  28.0,  60.0,  60.0,  60.0,  40.0,  40.0,  40.0);

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Distance measured the short way round the hue circle, signed so each side of the
// band can use its own reach.
float band(int i, float h) {
    float d = mod(h - BAND_H[i] + 540.0, 360.0) - 180.0;
    return 1.0 - smoothstep(0.0, d < 0.0 ? BAND_LO[i] : BAND_HI[i], abs(d));
}

// Weighted mean of the eight sliders at this pixel's hue. The weights already sum to
// 1 by construction; dividing keeps that true if the tables above are ever retuned.
float bandMix(float h) {
    float v[8] = float[8](red, orange, yellow, green, aqua, blue, purple, magenta);
    float sum = 0.0;
    float tot = 0.0;
    for (int i = 0; i < 8; ++i) {
        float w = band(i, h);
        sum += w * v[i];
        tot += w;
    }
    return tot > 1e-4 ? sum / tot : 0.0;
}

void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    vec3 hsv = rgb2hsv(c.rgb);
    float h = hsv.x * 360.0;
    float gate = smoothstep(0.04, 0.20, hsv.y);
    hsv.y = clamp(hsv.y * (1.0 + bandMix(h) * gate), 0.0, 1.0);
    fragColor = vec4(clamp(hsv2rgb(hsv), 0.0, 1.0), c.a);
}
