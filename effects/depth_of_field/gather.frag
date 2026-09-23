#version 330 core
// Pass 2: a disc gather. Each tap counts only if its own blur is big enough to reach this
// pixel, which is what keeps a sharp subject sharp while the background behind it melts.
// Background taps are also held to this pixel's own blur size, so a blurred background never
// drags colour in from a sharp foreground edge — the halo lens blur usually shows.
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

uniform float blur;
uniform float highlights;

const int kTaps = 48;
const float kGoldenAngle = 2.39996323;

void main()
{
    vec4 centre = texture(u_currentTexture, v_texCoord);
    // "Blur" is in pixels at 1080p, so a look carries across resolutions.
    float maxRadius = blur * u_resolution.y / 1080.0;
    if (maxRadius < 0.5) {
        fragColor = vec4(centre.rgb, 1.0);
        return;
    }
    float centreCoc = (centre.a - 0.5) * 2.0;
    float centreRadius = abs(centreCoc) * maxRadius;
    vec2 pixel = 1.0 / u_resolution;

    vec3 sum = centre.rgb;
    float weights = 1.0;
    for (int i = 1; i < kTaps; ++i) {
        float r = sqrt(float(i) / float(kTaps)) * maxRadius;
        float a = float(i) * kGoldenAngle;
        vec4 s = texture(u_currentTexture, v_texCoord + vec2(cos(a), sin(a)) * r * pixel);
        float coc = (s.a - 0.5) * 2.0;
        float reach = clamp(abs(coc) * maxRadius - r + 0.5, 0.0, 1.0);
        if (coc < centreCoc)
            reach *= clamp(centreRadius / max(r, 1.0), 0.0, 1.0);
        float luma = dot(s.rgb, vec3(0.2126, 0.7152, 0.0722));
        float w = reach * (1.0 + highlights * pow(luma, 3.0) * 6.0);
        sum += s.rgb * w;
        weights += w;
    }
    fragColor = vec4(sum / weights, 1.0);
}
