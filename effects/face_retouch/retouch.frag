#version 330 core
// Face Retouch, pass 4: skin smoothing, sharpening, whitening and skin tone.
// Ported from GPUPixel's BeautyFaceUnitFilter, Copyright (c) 2021 PixPark, Apache License 2.0.
// See NOTICE. Modified: smoothing and whitening are masked to the skin of the tracked face, the
// whitening blend starts from the untouched frame so that 0 means off, and the 1D gray LUT is
// read at texel centres because Drift's package textures wrap.
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture; // source
uniform sampler2D u_texture1;       // mean
uniform sampler2D u_texture2;       // var
uniform sampler2D u_texture3;       // lookup_gray, 256x1
uniform sampler2D u_texture4;       // lookup_origin, 64x64, 16 levels
uniform sampler2D u_texture5;       // lookup_skin, 64x64, 16 levels
uniform sampler2D u_texture6;       // lookup_light, 512x512, 64 levels
uniform vec2 u_resolution;
uniform float u_faceValid; uniform float u_faceHasContours; uniform float u_faceHasMesh;
uniform vec2 u_faceOval[36];
uniform vec2 u_faceLipOuter[20];
uniform vec2 u_faceEyeLeft[16];
uniform vec2 u_faceEyeRight[16];
uniform vec2 u_faceBrowLeft[10];
uniform vec2 u_faceBrowRight[10];
uniform float u_faceRx;
uniform float u_faceCheekLeftX; uniform float u_faceCheekLeftY;
uniform float u_faceCheekRightX; uniform float u_faceCheekRightY;
uniform float smoothing; uniform float sharpen; uniform float whitening; uniform float skinFeather;
uniform float skinTone; uniform vec3 toneColor;

const float levelRangeInv = 1.02657;
const float levelBlack = 0.0258820;
const float alpha = 0.7;

vec2 toLocal(vec2 uv, float aspect) { return vec2(uv.x, uv.y * aspect); }

// The beauty packages' shared helper: a signed distance to a closed contour loop. A
// macro because GLSL 3.30 cannot pass a uniform array of unknown size to a function.
#define SD_POLY(NAME, ARR, N)                                                                      \
    float NAME(vec2 p) {                                                                           \
        vec2 d0 = p - ARR[0];                                                                      \
        float d = dot(d0, d0);                                                                     \
        float s = 1.0;                                                                             \
        for (int i = 0, j = N - 1; i < N; j = i, i++) {                                            \
            vec2 e = ARR[j] - ARR[i];                                                              \
            vec2 w = p - ARR[i];                                                                   \
            vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);                               \
            d = min(d, dot(b, b));                                                                 \
            bvec3 c = bvec3(p.y >= ARR[i].y, p.y < ARR[j].y, e.x * w.y > e.y * w.x);               \
            if (all(c) || all(not(c))) s = -s;                                                     \
        }                                                                                          \
        return s * sqrt(d);                                                                        \
    }

SD_POLY(sdOval, u_faceOval, 36)
SD_POLY(sdLips, u_faceLipOuter, 20)
SD_POLY(sdEyeL, u_faceEyeLeft, 16)
SD_POLY(sdEyeR, u_faceEyeRight, 16)
SD_POLY(sdBrowL, u_faceBrowLeft, 10)
SD_POLY(sdBrowR, u_faceBrowRight, 10)

// GPUPixel's 4x4-tile, 16-level colour LUT.
vec3 lut16(sampler2D lut, vec3 c) {
    c = clamp(c, 0.0, 1.0);
    float blue = c.b * 15.0;
    vec2 q1, q2;
    q1.y = floor(floor(blue) * 0.25);
    q1.x = floor(blue) - q1.y * 4.0;
    q2.y = floor(ceil(blue) * 0.25);
    q2.x = ceil(blue) - q2.y * 4.0;
    vec2 pos = c.rg * 0.234375 + 0.0078125;
    return mix(texture(lut, q1 * 0.25 + pos).rgb, texture(lut, q2 * 0.25 + pos).rgb, fract(blue));
}

// The 8x8-tile, 64-level 512x512 LUT.
vec3 lut64(sampler2D lut, vec3 c) {
    c = clamp(c, 0.0, 1.0);
    float blue = c.b * 63.0;
    vec2 q1, q2;
    q1.y = floor(floor(blue) / 8.0);
    q1.x = floor(blue) - q1.y * 8.0;
    q2.y = floor(ceil(blue) / 8.0);
    q2.x = ceil(blue) - q2.y * 8.0;
    vec2 pos = 0.5 / 512.0 + (1.0 / 8.0 - 1.0 / 512.0) * c.rg;
    return mix(texture(lut, q1 / 8.0 + pos).rgb, texture(lut, q2 / 8.0 + pos).rgb, fract(blue));
}

float gray(float v, int channel) {
    vec4 t = texture(u_texture3, vec2((clamp(v, 0.0, 1.0) * 255.0 + 0.5) / 256.0, 0.5));
    return t[channel];
}

void main() {
    vec4 src = texture(u_currentTexture, v_texCoord);
    if (u_faceValid < 0.5 || u_faceHasContours < 0.5 || u_faceHasMesh < 0.5) { fragColor = src; return; }

    float aspect = u_resolution.y / u_resolution.x;
    vec2 p = toLocal(v_texCoord, aspect);

    // Skin is the oval minus every feature, so lashes, brows and the lip line stay sharp.
    // Whitening gets twice the feather: a brightness step at the jaw reads far more than a
    // texture step does.
    float fw = max(skinFeather * 0.12 * u_faceRx, 1e-5);
    float ovalD = sdOval(p);
    float face = 1.0 - smoothstep(-fw, fw * 0.4, ovalD);
    float faceWide = 1.0 - smoothstep(-2.0 * fw, 0.8 * fw, ovalD);
    if (faceWide <= 0.0) { fragColor = src; return; }
    float featureD = min(min(sdLips(p), min(sdEyeL(p), sdEyeR(p))), min(sdBrowL(p), sdBrowR(p)));
    float guard = max(u_faceRx * 0.05, 1e-5);
    float notFeature = smoothstep(-guard * 0.2, guard, featureD);
    float skin = face * notFeature;
    float skinWide = faceWide * notFeature;

    vec3 iColor = src.rgb;
    vec3 meanColor = texture(u_texture1, v_texCoord).rgb;
    vec3 varColor = texture(u_texture2, v_texCoord).rgb;

    // GPUPixel's skin heuristic: skin is red-dominant and not too dark.
    float redSkin = clamp((min(iColor.r, meanColor.r - 0.1) - 0.2) * 4.0, 0.0, 1.0);

    // Edge-preserving smoothing: mix toward the mean, less where the local variance is high.
    float theta = 0.1;
    float meanVar = (varColor.r + varColor.g + varColor.b) / 3.0;
    float kMin = clamp((1.0 - meanVar / (meanVar + theta)) * redSkin * smoothing * skin, 0.0, 1.0);
    vec3 color = mix(iColor, meanColor, kMin);

    // 3x3 tent high-pass, the whole face including the eyes, where it does the most good.
    vec2 px = 1.0 / u_resolution;
    vec3 sum = 0.25 * iColor;
    sum += 0.125 * (texture(u_currentTexture, v_texCoord + vec2(-px.x, 0.0)).rgb
                    + texture(u_currentTexture, v_texCoord + vec2(px.x, 0.0)).rgb
                    + texture(u_currentTexture, v_texCoord + vec2(0.0, -px.y)).rgb
                    + texture(u_currentTexture, v_texCoord + vec2(0.0, px.y)).rgb);
    sum += 0.0625 * (texture(u_currentTexture, v_texCoord + px).rgb
                     + texture(u_currentTexture, v_texCoord - px).rgb
                     + texture(u_currentTexture, v_texCoord + vec2(-px.x, px.y)).rgb
                     + texture(u_currentTexture, v_texCoord + vec2(px.x, -px.y)).rgb);
    color += sharpen * (iColor - sum) * 2.0 * face;

    // Whitening: levels, the gray curve, then the origin, skin and light LUTs. GPUPixel jumps
    // straight to the full chain for any whiten above 0; easing in from the frame keeps 0 off.
    float w = whitening * redSkin * skinWide;
    if (w > 0.0) {
        vec3 epm = clamp(color, 0.0, 1.0);
        vec3 c = clamp((epm - vec3(levelBlack)) * levelRangeInv, 0.0, 1.0);
        vec3 texel = vec3(gray(c.r, 0), gray(c.g, 1), gray(c.b, 2));
        texel = mix(c, texel, 0.5);
        texel = mix(epm, texel, alpha);
        texel = mix(lut16(u_texture4, texel), c, alpha);
        vec3 base = clamp(lut16(u_texture5, texel), 0.0, 1.0);
        vec3 light = lut64(u_texture6, base);
        color = mix(color, mix(base, light, whitening), w);
    }

    // Skin tone (Drift's, not GPUPixel's): the face's own skin colour, read from the smoothed
    // frame at both cheeks, is scaled toward the picked tone. A per-channel gain rather than a mix
    // keeps shading, pores and highlights, and the contour mask alone decides where skin is, so
    // it works on every complexion. The gain is bounded so a dark frame cannot blow out.
    float toneW = skinTone * skinWide;
    if (toneW > 0.0) {
        vec3 ref = 0.5 * (texture(u_texture1, vec2(u_faceCheekLeftX, u_faceCheekLeftY)).rgb
                          + texture(u_texture1, vec2(u_faceCheekRightX, u_faceCheekRightY)).rgb);
        vec3 gain = clamp(toneColor / max(ref, vec3(0.02)), vec3(0.25), vec3(3.0));
        color = mix(color, color * gain, toneW);
    }

    fragColor = vec4(clamp(color, 0.0, 1.0), src.a);
}
