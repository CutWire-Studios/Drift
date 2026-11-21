#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

void main() {
    vec2 o = 1.0 / u_resolution;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; ++i)
        sum += texture(u_currentTexture, v_texCoord + vec2((i & 1) == 0 ? -1.0 : 1.0, (i & 2) == 0 ? -1.0 : 1.0) * o).rgb;
    fragColor = vec4(sum * 0.25, 1.0);
}
