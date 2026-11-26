#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_choke; // pixels: negative grows the matte, positive shrinks it

// Min (erode) or max (dilate) of alpha over a disc, sampled on at most three rings of
// eight taps plus the centre. Radius is in output pixels, so it holds at any resolution.
void main()
{
    vec4 c = texture(u_currentTexture, v_texCoord);
    float r = min(abs(u_choke), 20.0);
    if (r <= 0.0) {
        fragColor = c;
        return;
    }
    int rings = r <= 1.5 ? 1 : (r <= 4.0 ? 2 : 3);
    float a = c.a;
    for (int k = 1; k <= 3; ++k) {
        if (k > rings) break;
        float rad = r * float(k) / float(rings);
        for (int i = 0; i < 8; ++i) {
            float ang = 0.7853982 * float(i) + (k == 2 ? 0.3926991 : 0.0);
            vec2 uv = v_texCoord + vec2(cos(ang), sin(ang)) * rad / u_resolution;
            float t = texture(u_currentTexture, uv).a;
            a = u_choke < 0.0 ? max(a, t) : min(a, t);
        }
    }
    fragColor = vec4(c.rgb, a);
}
