#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture; uniform float u_time;
uniform float intensity; uniform float hue; uniform float angle;
uniform float pattern;
uniform float speed; uniform float softness; uniform float flicker;

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

void main() {
    vec4 src = texture(u_currentTexture, v_texCoord);
    if (intensity <= 1e-5) {
        fragColor = src;
        return;
    }

    int pat = int(pattern + 0.5);
    if (pat > 0) {
        // Shaped leaks: angle rotates the whole shape about the frame centre.
        float ra = angle * 6.2831853;
        vec2 q = v_texCoord - 0.5;
        q = vec2(q.x * cos(ra) + q.y * sin(ra), -q.x * sin(ra) + q.y * cos(ra));
        vec2 uv = q + 0.5;
        float phase = u_time * speed;
        float spread = max(softness, 0.02) / 0.3;
        float flick1 = 1.0 - flicker * 0.5 * (0.5 + 0.5 * sin(u_time * 37.0));
        vec3 c0 = hsv2rgb(vec3(fract(hue), 0.75, 1.0));
        vec3 c1 = hsv2rgb(vec3(fract(hue - 0.06), 0.8, 1.0));
        vec3 c2 = hsv2rgb(vec3(fract(hue + 0.05), 0.65, 1.0));
        vec3 leak;
        if (pat == 1) {
            float cx = -0.10 + 0.23 * sin(phase * 0.46 + 0.4);
            float cy = 0.35 + 0.30 * sin(phase * 0.32 + 1.3);
            vec2 d = (uv - vec2(cx, cy)) / vec2(spread * 0.67, 0.92);
            float mainLeak = exp(-dot(d, d) * 2.7);
            float rim = exp(-pow((uv.x - 1.13 - 0.09 * sin(phase * 0.65)) / (0.30 * spread), 2.0) * 1.6);
            float stripe = exp(-pow((uv.x - 0.14 - 0.03 * sin(phase)) / (0.13 * spread), 2.0)) * 0.22;
            leak = c0 * mainLeak + c1 * rim + c2 * stripe;
        } else if (pat == 2) {
            vec2 corner = vec2(1.0 + 0.06 * sin(phase * 0.7), 1.0 + 0.06 * sin(phase * 0.5 + 1.0));
            float r = length((uv - corner) * vec2(1.0, 1.0)) / (0.75 * spread);
            float core = exp(-r * r * 2.2);
            float halo = exp(-r * r * 0.6) * 0.35;
            leak = c0 * core * 1.2 + c1 * halo;
        } else {
            float edge = 1.0 + 0.04 * sin(phase * 0.8);
            float w = 0.22 * spread;
            float band = exp(-pow((uv.x - edge) / w, 2.0));
            float streak = 0.75 + 0.25 * sin(uv.y * 5.0 + phase * 0.6);
            float inner = exp(-pow((uv.x - 0.93 - 0.02 * sin(phase)) / (w * 0.35), 2.0)) * 0.5;
            leak = c0 * band * streak + c1 * inner;
        }
        float amt = clamp(intensity, 0.0, 1.0) * flick1;
        vec3 col = 1.0 - (1.0 - clamp(src.rgb, 0.0, 1.0)) * exp(-leak * (1.8 * amt));
        fragColor = vec4(clamp(col, 0.0, 1.0), src.a);
        return;
    }

    vec2 p = v_texCoord - 0.5;
    float a = angle * 6.2831853;
    float axis = p.x * cos(a) + p.y * sin(a);

    // The leak sweeps past rather than sitting still; travel runs off both edges so the
    // wrap at fract() == 0 happens off-frame instead of as a visible jump.
    float travel = fract(u_time * speed * 0.15);
    float pos = mix(-0.9, 0.9, travel);
    float soft = max(softness, 0.02);

    float band = exp(-pow((axis - pos) / soft, 2.0));
    float band2 = exp(-pow((axis - pos - 0.18) / (soft * 0.4), 2.0)) * 0.6;

    // Real leaks come off a lamp with mains ripple, not a steady source.
    float flick = 1.0 - flicker * 0.5 * (0.5 + 0.5 * sin(u_time * 37.0));

    vec3 leak = hsv2rgb(vec3(fract(hue), 0.75, 1.0));
    vec3 add = leak * (band + band2) * intensity * flick;
    add = clamp(add, 0.0, 1.0);

    vec3 outc = 1.0 - (1.0 - clamp(src.rgb, 0.0, 1.0)) * (1.0 - add);
    fragColor = vec4(outc, src.a);
}
