#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture; uniform float highlights;
void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    float w = smoothstep(0.45, 1.0, l);
    // Headroom in the direction of travel. Recovery gets the shorter leash: a
    // bright pixel has everything below it, and a full pull would otherwise drag
    // the highlights all the way to black.
    vec3 room = highlights >= 0.0 ? (1.0 - c.rgb) : c.rgb * 0.6;
    fragColor = vec4(clamp(c.rgb + highlights * w * room, 0.0, 1.0), c.a);
}
