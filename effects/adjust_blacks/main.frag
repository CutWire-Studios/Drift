#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture; uniform float blacks;
void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    float w = 1.0 - smoothstep(0.0, 0.65, l);
    // Positive lifts the floor (washed, filmic), negative crushes it.
    float bp = -blacks * 0.25;
    vec3 v = (c.rgb - bp) / max(1.0 - bp, 0.05);
    fragColor = vec4(clamp(mix(c.rgb, v, w), 0.0, 1.0), c.a);
}
