#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float mode;
uniform float strength;
uniform float regionX;
uniform float regionY;
uniform float regionW;
uniform float regionH;

vec4 unpremul(vec4 c) {
    return vec4(c.rgb / max(c.a, 1e-4), c.a);
}

void main() {
    vec2 rel = (v_texCoord - vec2(regionX, regionY)) / max(vec2(regionW, regionH), vec2(1e-6));
    float sigma = strength * 0.04 * max(u_resolution.x, u_resolution.y);
    if (mode > 0.5 || sigma < 0.5 || rel.x < 0.0 || rel.x > 1.0 || rel.y < 0.0 || rel.y > 1.0) {
        fragColor = unpremul(texture(u_currentTexture, v_texCoord));
        return;
    }
    float reach = 3.0 * sigma;
    float step = max(1.0, reach / 128.0);
    float inv2s2 = 0.5 / (sigma * sigma);
    int n = int(min(ceil(reach / (2.0 * step)), 64.0));
    vec2 dir = vec2(0.0, 1.0 / u_resolution.y);

    vec4 result = texture(u_currentTexture, v_texCoord);
    float wsum = 1.0;
    for (int i = 1; i <= 64; ++i) {
        if (i > n)
            break;
        float a = (2.0 * float(i) - 1.0) * step;
        float b = 2.0 * float(i) * step;
        float wa = exp(-a * a * inv2s2);
        float wb = exp(-b * b * inv2s2);
        float w = wa + wb;
        float x = (a * wa + b * wb) / w;
        result += (texture(u_currentTexture, v_texCoord + dir * x) + texture(u_currentTexture, v_texCoord - dir * x)) * w;
        wsum += 2.0 * w;
    }
    fragColor = unpremul(result / wsum);
}
