#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float mode;
uniform float strength;
uniform float regionX;
uniform float regionY;
uniform float regionW;
uniform float regionH;
uniform vec3 barColor;

void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    vec2 rel = (v_texCoord - vec2(regionX, regionY)) / max(vec2(regionW, regionH), vec2(1e-6));
    if (regionW <= 0.0 || regionH <= 0.0 || rel.x < 0.0 || rel.x > 1.0 || rel.y < 0.0 || rel.y > 1.0) {
        fragColor = c;
        return;
    }
    if (mode > 1.5) {
        fragColor = vec4(barColor, c.a);
    } else if (mode > 0.5) {
        float block = strength * 0.1 * max(u_resolution.x, u_resolution.y);
        if (block < 1.5) {
            fragColor = c;
            return;
        }
        vec2 cell = floor(v_texCoord * u_resolution / block);
        vec2 uv = (cell + 0.5) * block / u_resolution;
        vec4 m = texture(u_currentTexture, uv);
        fragColor = vec4(m.rgb, c.a);
    } else {
        vec4 b = texture(u_texture1, v_texCoord);
        fragColor = vec4(b.rgb, c.a);
    }
}
