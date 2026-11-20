#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;

uniform float u_keyMode;   // 0 = key on hue, 1 = key on brightness
uniform float u_keyHue;    // backdrop hue in degrees: 120 = green, 240 = blue
uniform float u_keyLuma;   // backdrop luma for brightness mode: 0 = black, 1 = white
uniform float u_tolerance; // how far off the key hue still counts as background
uniform float u_softness;  // width of the alpha ramp at the edge
uniform float u_spill;     // how hard to pull the key colour out of what remains

const vec3 kRec709 = vec3(0.2126, 0.7152, 0.0722);

// Chroma magnitude scales with brightness, so a shadowed fold of the screen reads
// as far less green than a lit one and survives the key. Dividing the score by
// luma removes that; the floor keeps near-black pixels — where chroma is mostly
// sensor noise — from being amplified into a false key.
const float kLumaFloor = 0.15;

vec2 chromaOf(vec3 c, float y)
{
    return vec2((c.b - y) * 0.5389, (c.r - y) * 0.6350);
}

vec3 hueToRgb(float degrees)
{
    float h = mod(degrees, 360.0) / 60.0;
    float x = 1.0 - abs(mod(h, 2.0) - 1.0);
    if (h < 1.0) return vec3(1.0, x, 0.0);
    if (h < 2.0) return vec3(x, 1.0, 0.0);
    if (h < 3.0) return vec3(0.0, 1.0, x);
    if (h < 4.0) return vec3(0.0, x, 1.0);
    if (h < 5.0) return vec3(x, 0.0, 1.0);
    return vec3(1.0, 0.0, x);
}

void main()
{
    vec4 src = texture(u_currentTexture, v_texCoord);

    // Black, white and grey have no hue, so the hue key below can never select them (#605).
    // Brightness mode keys on luma distance instead; there is no hue to spill.
    if (u_keyMode > 0.5) {
        float dist = abs(dot(src.rgb, kRec709) - u_keyLuma);
        float threshold = mix(0.02, 0.5, clamp(u_tolerance, 0.0, 1.0));
        float ramp = mix(0.02, 0.3, clamp(u_softness, 0.0, 1.0));
        float lumaAlpha = smoothstep(threshold - ramp * 0.5, threshold + ramp * 0.5, dist);
        fragColor = vec4(src.rgb, src.a * lumaAlpha);
        return;
    }

    // Effect parameters are plain floats, so the key is a hue rather than a picked
    // colour: we never learn how saturated the actual backdrop is. Splitting the
    // pixel's chroma into a component along the key hue and one across it keys on
    // hue alone, which holds up whether the screen is vivid or washed out.
    vec3 keyRgb = hueToRgb(u_keyHue);
    vec2 keyDir = normalize(chromaOf(keyRgb, dot(keyRgb, kRec709)));

    float y = dot(src.rgb, kRec709);
    vec2 pc = chromaOf(src.rgb, y);
    float along = dot(pc, keyDir);
    float across = length(pc - along * keyDir);

    float norm = max(y, kLumaFloor);
    float score = (along - across) / norm;

    // Tolerance sets the centre of the alpha ramp rather than its start. A pixel on
    // an antialiased or motion-blurred edge is part screen and part subject, so its
    // score lands between the two; a ramp starting at the screen's score would leave
    // that whole blend opaque and ring the subject with a hard fringe. Centred, the
    // blend falls through the ramp and comes out partly transparent, as it should.
    float centre = mix(0.75, 0.05, clamp(u_tolerance, 0.0, 1.0));
    float width = mix(0.05, 0.60, clamp(u_softness, 0.0, 1.0));
    float alpha = 1.0 - smoothstep(centre - width * 0.5, centre + width * 0.5, score);

    // Screen bounce tints what survives, worst along the subject's edge. Subtract the
    // key-hue component of the chroma and rebuild RGB: luma and the across-axis colour
    // survive, so a warm or blue foreground keeps its character instead of going grey.
    // Removal scales up as alpha falls — a mostly-screen pixel is mostly screen colour,
    // and alpha here is straight, not premultiplied, so that colour still tints the
    // composite and would otherwise ring the subject with a halo.
    float spillStrength = mix(1.0, clamp(u_spill, 0.0, 1.0), alpha);
    vec2 corrected = pc - max(along, 0.0) * spillStrength * keyDir;
    float r = corrected.y / 0.6350 + y;
    float b = corrected.x / 0.5389 + y;
    float g = (y - 0.2126 * r - 0.0722 * b) / 0.7152;

    fragColor = vec4(clamp(vec3(r, g, b), 0.0, 1.0), src.a * alpha);
}
