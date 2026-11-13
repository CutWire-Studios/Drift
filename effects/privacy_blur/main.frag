#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float mode;
uniform float strength;
uniform vec3 barColor;

void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    float m = driftMask(v_texCoord);
    if (m <= 0.0) {
        fragColor = c;
        return;
    }
    vec3 hidden = c.rgb;
    if (mode > 1.5) {
        hidden = barColor;
    } else if (mode > 0.5) {
        float block = strength * 0.1 * max(u_resolution.x, u_resolution.y);
        if (block >= 1.5) {
            vec2 cell = floor(v_texCoord * u_resolution / block);
            vec2 uv = (cell + 0.5) * block / u_resolution;
            hidden = texture(u_currentTexture, uv).rgb;
        }
    } else {
        hidden = texture(u_texture1, v_texCoord).rgb;
    }
    fragColor = vec4(mix(c.rgb, hidden, m), c.a);
}
