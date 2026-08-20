#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture; uniform float tint;
void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    // Channel gain rather than an offset: white balance is a scaling, and adding a
    // constant would tint the black background before the midtones had moved.
    vec3 gain = vec3(1.0 + tint * 0.18, 1.0 - tint * 0.22, 1.0 + tint * 0.18);
    fragColor = vec4(clamp(c.rgb * gain, 0.0, 1.0), c.a);
}
