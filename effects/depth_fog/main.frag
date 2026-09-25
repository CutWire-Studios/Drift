#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;

uniform vec3 fogColor;
uniform float density;
uniform float start;
uniform float ground;
uniform float amount;

void main()
{
    vec4 src = texture(u_currentTexture, v_texCoord);
    if (u_hasDepth < 0.5) {
        fragColor = src;
        return;
    }
    // Distance past the start of the fog, 0..1 across the rest of the scene.
    float away = 1.0 - driftDepthGuided(v_texCoord, u_currentTexture);
    float into = max(away - start, 0.0) / max(1.0 - start, 1e-3);
    float fog = 1.0 - exp(-density * 3.0 * into);
    // Low-lying fog thins towards the top of the frame.
    fog *= mix(1.0, v_texCoord.y, ground);
    fragColor = vec4(mix(src.rgb, fogColor, fog * amount), src.a);
}
