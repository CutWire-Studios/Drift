#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;

uniform float colorize;
uniform float edges;
uniform float amount;

// Polynomial fit of Google's Turbo colour map.
vec3 turbo(float t)
{
    const vec4 kr = vec4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
    const vec4 kg = vec4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
    const vec4 kb = vec4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
    const vec2 kr2 = vec2(-152.94239396, 59.28637943);
    const vec2 kg2 = vec2(4.27729857, 2.82956604);
    const vec2 kb2 = vec2(-89.90310912, 27.34824973);
    t = clamp(t, 0.0, 1.0);
    vec4 v4 = vec4(1.0, t, t * t, t * t * t);
    vec2 v2 = v4.zw * v4.z;
    return vec3(dot(v4, kr) + dot(v2, kr2), dot(v4, kg) + dot(v2, kg2), dot(v4, kb) + dot(v2, kb2));
}

void main()
{
    vec4 src = texture(u_currentTexture, v_texCoord);
    if (u_hasDepth < 0.5) {
        fragColor = src;
        return;
    }
    float d = edges > 0.5 ? driftDepthGuided(v_texCoord, u_currentTexture) : driftDepth(v_texCoord);
    vec3 view = colorize > 0.5 ? turbo(d) : vec3(d);
    fragColor = vec4(mix(src.rgb, view, amount), src.a);
}
