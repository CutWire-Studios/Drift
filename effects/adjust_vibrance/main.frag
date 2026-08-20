#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture; uniform float vibrance;
void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    float mx = max(c.r, max(c.g, c.b));
    float mn = min(c.r, min(c.g, c.b));
    float sat = mx - mn;
    float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    float amt = vibrance * (1.0 - sat);
    // Warm tones with green above blue are the skin range; a full-strength
    // vibrance there goes orange before anything else has moved.
    float skin = smoothstep(0.0, 0.35, c.r - c.b) * step(c.b, c.g);
    amt *= 1.0 - 0.5 * skin;
    fragColor = vec4(clamp(mix(vec3(l), c.rgb, 1.0 + amt), 0.0, 1.0), c.a);
}
