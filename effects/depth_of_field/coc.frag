#version 330 core
// Pass 1: the circle of confusion. Colour passes through; alpha carries the signed blur size,
// 0.5 meaning in focus, above it nearer than the focus distance, below it farther.
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;

uniform float focusDepth;
uniform float focusRange;
uniform float nearBlur;
uniform float autoFocus;
uniform float focusX;
uniform float focusY;

// Averaged over a small cross so a focus point on an edge does not flicker between the two sides.
float focusAt(vec2 uv)
{
    vec2 t = 2.0 / u_depthResolution;
    return (driftDepth(uv) * 2.0 + driftDepth(uv + vec2(t.x, 0.0)) + driftDepth(uv - vec2(t.x, 0.0))
            + driftDepth(uv + vec2(0.0, t.y)) + driftDepth(uv - vec2(0.0, t.y))) / 6.0;
}

void main()
{
    vec4 src = texture(u_currentTexture, v_texCoord);
    if (u_hasDepth < 0.5) {
        fragColor = vec4(src.rgb, 0.5);
        return;
    }
    float focus = autoFocus > 0.5 ? focusAt(vec2(focusX, focusY)) : focusDepth;
    float d = driftDepthGuided(v_texCoord, u_currentTexture);
    float offset = d - focus;
    // Outside the sharp band, blur grows over the next 0.25 of depth to full size.
    float coc = clamp((abs(offset) - focusRange) / 0.25, 0.0, 1.0);
    if (offset > 0.0)
        coc *= nearBlur;
    fragColor = vec4(src.rgb, 0.5 + 0.5 * sign(offset) * coc);
}
