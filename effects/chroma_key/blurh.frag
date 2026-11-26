#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_matteBlur; // pixels, gaussian sigma is half of this

void main()
{
    vec4 c = texture(u_currentTexture, v_texCoord);
    float r = min(u_matteBlur, 20.0);
    if (r <= 0.0) {
        fragColor = c;
        return;
    }
    float sum = 0.0, wsum = 0.0;
    for (int i = -6; i <= 6; ++i) {
        float w = exp(-0.5 * float(i * i) / 9.0);
        vec2 uv = v_texCoord + vec2(r * float(i) / 6.0, 0.0) / u_resolution;
        sum += w * texture(u_currentTexture, uv).a;
        wsum += w;
    }
    fragColor = vec4(c.rgb, sum / wsum);
}
