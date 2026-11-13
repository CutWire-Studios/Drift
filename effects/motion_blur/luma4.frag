#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// Current luma in R and the previous frame's in G, box-filtered to the quarter-size buffer. The
// previous frame is the engine's own input until a history is recorded, so a first frame sees no motion.
void main() {
    vec2 o = 1.0 / u_resolution;
    float cur = 0.0;
    float prev = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 uv = v_texCoord + vec2((i & 1) == 0 ? -1.0 : 1.0, (i & 2) == 0 ? -1.0 : 1.0) * o;
        cur += luma(texture(u_currentTexture, uv).rgb);
        prev += luma(historyFrame(1.0, uv).rgb);
    }
    fragColor = vec4(cur * 0.25, prev * 0.25, 0.0, 1.0);
}
