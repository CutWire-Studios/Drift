#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture; uniform float shadows;
void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    // Rolls off at both ends: pure black is the Blacks slider's business, and
    // lifting it here would flatten a dark background into grey noise.
    float w = (1.0 - smoothstep(0.0, 0.55, l)) * smoothstep(0.0, 0.10, l);
    // Headroom in the direction of travel, on a per-direction leash: a dark pixel
    // has nearly the whole range above it, so an unscaled lift would take hair and
    // fabric to mid-grey before the slider was halfway.
    vec3 room = shadows >= 0.0 ? (1.0 - c.rgb) * 0.5 : c.rgb * 0.7;
    fragColor = vec4(clamp(c.rgb + shadows * w * room, 0.0, 1.0), c.a);
}
