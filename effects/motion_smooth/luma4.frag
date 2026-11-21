#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float multiplier;

float luma(vec3 c) {
    return dot(c.rgb, vec3(0.299, 0.587, 0.114));
}

const float kMultipliers[5] = float[5](2.0, 3.0, 4.0, 5.0, 8.0);

// The phase this output frame shows, quantised so the result moves like footage at fps * multiplier.
float phase() {
    float m = kMultipliers[int(clamp(floor(multiplier + 0.5), 0.0, 4.0))];
    return min(floor(sourcePhase() * m + 1e-4), m - 1.0) / m;
}

// The shown frame's luma in R and the next source frame's in G, box-filtered to the quarter-size buffer.
// Where nothing is interpolated (phase 0, the last frame, a scrub) the buffer is empty, so the matching
// passes find nothing to search and stay cheap.
void main() {
    if (phase() <= 0.0 || futureCount() < 0.5) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec2 o = 1.0 / u_resolution;
    float a = 0.0;
    float b = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 uv = v_texCoord + vec2((i & 1) == 0 ? -1.0 : 1.0, (i & 2) == 0 ? -1.0 : 1.0) * o;
        a += luma(texture(u_currentTexture, uv).rgb);
        b += luma(futureFrame(1.0, uv).rgb);
    }
    fragColor = vec4(a * 0.25, b * 0.25, 0.0, 1.0);
}
