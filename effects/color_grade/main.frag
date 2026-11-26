#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float exposure; uniform float contrast; uniform float highlights; uniform float shadows;
uniform float whites; uniform float blacks;
uniform float temperature; uniform float tint; uniform float saturation; uniform float vibrance;
uniform vec2 lift; uniform float liftLuma; uniform vec2 gamma; uniform float gammaLuma;
uniform vec2 gain; uniform float gainLuma;
uniform sampler2D curveMaster; uniform sampler2D curveR; uniform sampler2D curveG; uniform sampler2D curveB;
uniform sampler2D look; uniform float lookAmount;
uniform sampler2D curveHueHue; uniform sampler2D curveHueSat; uniform sampler2D curveHueLum;
uniform float hslRedHue; uniform float hslRedSat; uniform float hslRedLum;
uniform float hslOrangeHue; uniform float hslOrangeSat; uniform float hslOrangeLum;
uniform float hslYellowHue; uniform float hslYellowSat; uniform float hslYellowLum;
uniform float hslGreenHue; uniform float hslGreenSat; uniform float hslGreenLum;
uniform float hslAquaHue; uniform float hslAquaSat; uniform float hslAquaLum;
uniform float hslBlueHue; uniform float hslBlueSat; uniform float hslBlueLum;
uniform float hslPurpleHue; uniform float hslPurpleSat; uniform float hslPurpleLum;
uniform float hslMagentaHue; uniform float hslMagentaSat; uniform float hslMagentaLum;
uniform float qualifierEnabled; uniform float qualifierShowMatte; uniform float qualifierInvert;
uniform float qualifierHue; uniform float qualifierHueWidth; uniform float qualifierHueSoft;
uniform float qualifierSatLow; uniform float qualifierSatHigh; uniform float qualifierSatSoft;
uniform float qualifierLumLow; uniform float qualifierLumHigh; uniform float qualifierLumSoft;
// Set by the runtime when a curve bakes to the identity, or when no look resolves.
uniform float curveMaster_identity; uniform float curveR_identity; uniform float curveG_identity;
uniform float curveB_identity; uniform float curveHueHue_identity; uniform float curveHueSat_identity;
uniform float curveHueLum_identity; uniform float look_identity;

float Y(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 srgbToLinear(vec3 c) {
    return mix(pow(max((c + 0.055) / 1.055, 0.0), vec3(2.4)), c / 12.92, lessThanEqual(c, vec3(0.04045)));
}

vec3 linearToSrgb(vec3 c) {
    return mix(1.055 * pow(max(c, 0.0), vec3(1.0 / 2.4)) - 0.055, c * 12.92, lessThanEqual(c, vec3(0.0031308)));
}

// Krystek's rational fit of the Planckian locus in CIE 1960 uv, as linear sRGB with Y = 1.
vec3 planckRGB(float T) {
    float u = (0.860117757 + 1.54118254e-4 * T + 1.28641212e-7 * T * T) / (1.0 + 8.42420235e-4 * T + 7.08145163e-7 * T * T);
    float v = (0.317398726 + 4.22806245e-5 * T + 4.20481691e-8 * T * T) / (1.0 - 2.89741816e-5 * T + 1.61456053e-7 * T * T);
    float den = 2.0 * u - 8.0 * v + 4.0;
    float x = 3.0 * u / den;
    float y = 2.0 * v / den;
    vec3 XYZ = vec3(x / y, 1.0, (1.0 - x - y) / y);
    return mat3(3.2406, -0.9689, 0.0557, -1.5372, 1.8758, -0.2040, -0.4986, 0.0415, 1.0570) * XYZ;
}

vec3 off(vec2 v) {
    vec3 yiq = vec3(0.0, 0.5957 * v.x, 0.5226 * v.y);
    return vec3(yiq.x + 0.956 * yiq.y + 0.619 * yiq.z,
                yiq.x - 0.272 * yiq.y - 0.647 * yiq.z,
                yiq.x - 1.106 * yiq.y + 1.703 * yiq.z);
}

float cv(sampler2D s, float x) {
    return texture(s, vec2((clamp(x, 0.0, 1.0) * 255.0 + 0.5) / 256.0, 0.5)).r;
}

float hueOf(vec3 c, float mx, float chroma) {
    float d = max(chroma, 1e-5);
    float h;
    if (c.r >= mx) h = (c.g - c.b) / d;
    else if (c.g >= mx) h = 2.0 + (c.b - c.r) / d;
    else h = 4.0 + (c.r - c.g) / d;
    return fract(h / 6.0);
}

// Rotates around the grey axis, so neutrals stay put; luma is then restored exactly.
vec3 hueRotate(vec3 c, float turns) {
    float a = turns * 6.2831853;
    const vec3 n = vec3(0.57735027);
    vec3 r = c * cos(a) + cross(n, c) * sin(a) + n * dot(n, c) * (1.0 - cos(a));
    return r + (Y(c) - Y(r));
}

// The bake rounds y = 0.5 to byte 128, so a flat hue curve reads back as 128/255, not 0.5.
float hueCv(sampler2D s, float identity, float x) {
    return identity > 0.5 ? 0.5 : cv(s, x) + 0.5 - 128.0 / 255.0;
}

vec3 hueGrade(vec3 c) {
    float mx = max(c.r, max(c.g, c.b));
    float chroma = mx - min(c.r, min(c.g, c.b));
    // Absolute chroma, not chroma / max: near-black noise has a high HSV saturation.
    float sw = smoothstep(0.0, 0.2, chroma);
    float h = hueOf(c, mx, chroma);

    const float centre[9] = float[9](0.0, 1.0 / 12.0, 1.0 / 6.0, 1.0 / 3.0, 0.5, 0.625, 0.75, 5.0 / 6.0, 1.0);
    float hueP[9] = float[9](hslRedHue, hslOrangeHue, hslYellowHue, hslGreenHue, hslAquaHue, hslBlueHue, hslPurpleHue, hslMagentaHue, hslRedHue);
    float satP[9] = float[9](hslRedSat, hslOrangeSat, hslYellowSat, hslGreenSat, hslAquaSat, hslBlueSat, hslPurpleSat, hslMagentaSat, hslRedSat);
    float lumP[9] = float[9](hslRedLum, hslOrangeLum, hslYellowLum, hslGreenLum, hslAquaLum, hslBlueLum, hslPurpleLum, hslMagentaLum, hslRedLum);
    float wHue = 0.0, wSat = 0.0, wLum = 0.0;
    for (int i = 0; i < 8; ++i) {
        if (h >= centre[i] && h < centre[i + 1]) {
            float t = smoothstep(centre[i], centre[i + 1], h);
            wHue = mix(hueP[i], hueP[i + 1], t);
            wSat = mix(satP[i], satP[i + 1], t);
            wLum = mix(lumP[i], lumP[i + 1], t);
        }
    }

    c = hueRotate(c, hueCv(curveHueHue, curveHueHue_identity, h) - 0.5 + wHue / 12.0);

    float satMul = mix(1.0, 2.0 * hueCv(curveHueSat, curveHueSat_identity, h) * max(1.0 + wSat, 0.0), sw);
    float yc = Y(c);
    c = yc + (c - yc) * satMul;
    c *= exp2(sw * ((hueCv(curveHueLum, curveHueLum_identity, h) - 0.5) * 2.0 + wLum * 0.6));
    return c;
}

float rampUp(float v, float edge, float soft) {
    return soft <= 1e-4 ? step(edge, v) : smoothstep(edge - soft, edge, v);
}

float rampDown(float v, float edge, float soft) {
    return soft <= 1e-4 ? step(v, edge) : 1.0 - smoothstep(edge, edge + soft, v);
}

// 1 inside the keyed hue / saturation / luma ranges, falling to 0 across each softness. The defaults
// span everything, so enabling the qualifier alone keys the whole picture.
float qualifierMatte(vec3 c) {
    float mx = max(c.r, max(c.g, c.b));
    float chroma = mx - min(c.r, min(c.g, c.b));
    float hueDeg = hueOf(c, mx, chroma) * 360.0;
    float d = abs(mod(hueDeg - qualifierHue + 180.0, 360.0) - 180.0);
    float m = rampDown(d, 0.5 * qualifierHueWidth, qualifierHueSoft);
    float sat = chroma / max(mx, 1e-5);
    m *= rampUp(sat, qualifierSatLow, qualifierSatSoft) * rampDown(sat, qualifierSatHigh, qualifierSatSoft);
    float yl = Y(c);
    m *= rampUp(yl, qualifierLumLow, qualifierLumSoft) * rampDown(yl, qualifierLumHigh, qualifierLumSoft);
    return qualifierInvert > 0.5 ? 1.0 - m : m;
}

vec3 lutSample(sampler2D t, vec3 c) {
    float bz = c.b * 32.0;
    float b0 = floor(bz);
    float b1 = min(b0 + 1.0, 32.0);
    float f = bz - b0;
    float v = (c.g * 32.0 + 0.5) / 33.0;
    float u0 = (b0 * 33.0 + c.r * 32.0 + 0.5) / 1089.0;
    float u1 = (b1 * 33.0 + c.r * 32.0 + 0.5) / 1089.0;
    return mix(texture(t, vec2(u0, v)).rgb, texture(t, vec2(u1, v)).rgb, f);
}

// Schlick's bias on each side of the pivot: slope k at the pivot and 1/k at black and white, and
// contrast -c is the exact inverse of +c. Above white it continues with the slope it ends on.
const float kPivot = 0.435;
float contrastCurve(float x, float k) {
    float s = k - 1.0;
    if (x < kPivot) {
        float t = x / kPivot;
        return kPivot * t / (s * (1.0 - t) + 1.0);
    }
    if (x <= 1.0) {
        float t = (1.0 - x) / (1.0 - kPivot);
        return 1.0 - (1.0 - kPivot) * t / (s * (1.0 - t) + 1.0);
    }
    return 1.0 + (x - 1.0) / k;
}

float hermite(float x, float x0, float x1, float y0, float y1, float m0, float m1) {
    float h = x1 - x0;
    float t = (x - x0) / h;
    float t2 = t * t, t3 = t2 * t;
    return (2.0 * t3 - 3.0 * t2 + 1.0) * y0 + (t3 - 2.0 * t2 + t) * h * m0
         + (3.0 * t2 - 2.0 * t3) * y1 + (t3 - t2) * h * m1;
}

// Black and the top stay pinned and 0.5 is the hinge. Shadows sets the slope at black, Highlights
// the slope at the top, each within [0.5, 2], which keeps both Hermite segments monotone. Pulling
// Highlights down also raises the top towards the brightest luma Exposure can produce, so
// overexposed values roll back under 1 rather than clipping.
float shadowsHighlights(float y, float top) {
    const float hinge = 0.5;
    if (y < hinge)
        return hermite(y, 0.0, hinge, 0.0, hinge, exp2(shadows), 1.0);
    float d = (1.0 - hinge) / (top - hinge);
    float m1 = d * d * exp2(-highlights);
    if (y <= top)
        return hermite(y, hinge, top, hinge, 1.0, 1.0, m1);
    return 1.0 + (y - top) * m1;
}

void main() {
    vec4 src = texture(u_currentTexture, v_texCoord);
    float a = max(src.a, 1e-5);
    vec3 c = src.rgb / a;
    vec3 original = c;

    vec3 lin = srgbToLinear(c);
    lin *= exp2(exposure);

    // Linear in mireds around 6500 K: about 4200 K at +1 and 14500 K at -1.
    float T = 1.0e6 / (1.0e6 / 6500.0 + 85.0 * temperature);
    vec3 wb = planckRGB(T) / planckRGB(6500.0);
    wb /= Y(wb);
    vec3 tintV = vec3(1.0, exp2(-0.5 * tint), 1.0);
    tintV /= Y(tintV);
    lin *= wb * tintV;
    c = linearToSrgb(max(lin, 0.0));

    float k = exp2(contrast);
    c = vec3(contrastCurve(c.r, k), contrastCurve(c.g, k), contrastCurve(c.b, k));

    float brightest = max(contrastCurve(linearToSrgb(vec3(exp2(exposure))).x, k), 1.0);
    float top = mix(1.0, brightest, max(-highlights, 0.0));
    float y0 = Y(c);
    float y1 = shadowsHighlights(y0, top);
    vec3 cScaled = c * (y1 / max(y0, 1e-4));
    vec3 cAdd = c + (y1 - y0);
    c = mix(cAdd, cScaled, smoothstep(0.03, 0.08, y0));

    float tlo = 0.15 * blacks;
    float thi = 1.0 + 0.15 * whites;
    c = tlo + c * (thi - tlo);

    vec3 L = 0.25 * (off(lift) + vec3(liftLuma));
    vec3 Gm = 0.5 * (off(gain) + vec3(gainLuma));
    vec3 G = 0.5 * (off(gamma) + vec3(gammaLuma));
    c = pow(max(c * (1.0 + Gm - L) + L, 1e-6), exp2(-G));

    if (curveMaster_identity < 0.5)
        c = vec3(cv(curveMaster, c.r), cv(curveMaster, c.g), cv(curveMaster, c.b));
    if (curveR_identity < 0.5) c.r = cv(curveR, c.r);
    if (curveG_identity < 0.5) c.g = cv(curveG, c.g);
    if (curveB_identity < 0.5) c.b = cv(curveB, c.b);

    c = hueGrade(c);

    float yy = Y(c);
    float s = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
    c = yy + (c - yy) * saturation * (1.0 + vibrance * (1.0 - s));

    // Over white: keep luma and give up chroma, so a warmed white clips to white, not yellow.
    yy = Y(c);
    float mx = max(c.r, max(c.g, c.b));
    if (mx > 1.0)
        c = yy >= 1.0 ? vec3(1.0) : yy + (c - yy) * ((1.0 - yy) / (mx - yy));
    c = clamp(c, 0.0, 1.0);

    if (look_identity < 0.5)
        c = mix(c, lutSample(look, c), lookAmount);

    if (qualifierEnabled > 0.5) {
        float m = qualifierMatte(original);
        c = qualifierShowMatte > 0.5 ? vec3(m) : mix(original, c, m);
    }

    fragColor = vec4(c * src.a, src.a);
}
