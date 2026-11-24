#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_matteBlur;
uniform float u_matteView; // 0 = composite, 1 = matte, 2 = status

void main()
{
    vec4 c = texture(u_currentTexture, v_texCoord);
    float a = c.a;
    float r = min(u_matteBlur, 20.0);
    if (r > 0.0) {
        float sum = 0.0, wsum = 0.0;
        for (int i = -6; i <= 6; ++i) {
            float w = exp(-0.5 * float(i * i) / 9.0);
            vec2 uv = v_texCoord + vec2(0.0, r * float(i) / 6.0) / u_resolution;
            sum += w * texture(u_currentTexture, uv).a;
            wsum += w;
        }
        a = sum / wsum;
    }

    if (u_matteView > 1.5) {
        // Fully removed reads blue, partly keyed magenta, solid keeps its grey.
        vec3 grey = vec3(a);
        vec3 tint = a <= 0.05 ? vec3(0.0, 0.2, 0.6)
                  : (a < 0.95 ? mix(vec3(0.9, 0.0, 0.5), vec3(1.0, 0.45, 0.8), a) : grey);
        fragColor = vec4(tint, 1.0);
    } else if (u_matteView > 0.5) {
        fragColor = vec4(vec3(a), 1.0);
    } else {
        fragColor = vec4(c.rgb, a);
    }
}
