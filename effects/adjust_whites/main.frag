#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture; uniform float whites;
void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    float w = smoothstep(0.30, 1.0, l);
    float wp = clamp(1.0 - whites * 0.55, 0.1, 2.0);
    fragColor = vec4(clamp(mix(c.rgb, c.rgb / wp, w), 0.0, 1.0), c.a);
}
