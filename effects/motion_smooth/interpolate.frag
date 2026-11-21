#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform sampler2D u_texture2;
uniform vec2 u_resolution;
uniform float multiplier;

vec2 flowTexel(sampler2D s, ivec2 p, ivec2 sz) {
    return unpackFlow(texelFetch(s, clamp(p, ivec2(0), sz - 1), 0));
}

vec2 flowAt(sampler2D s, vec2 uv) {
    ivec2 sz = textureSize(s, 0);
    vec2 f = uv * vec2(sz) - 0.5;
    ivec2 i = ivec2(floor(f));
    vec2 t = f - vec2(i);
    return mix(mix(flowTexel(s, i, sz), flowTexel(s, i + ivec2(1, 0), sz), t.x),
               mix(flowTexel(s, i + ivec2(0, 1), sz), flowTexel(s, i + ivec2(1, 1), sz), t.x), t.y);
}

const float kMultipliers[5] = float[5](2.0, 3.0, 4.0, 5.0, 8.0);

// The phase this output frame shows, quantised so the result moves like footage at fps * multiplier.
float phase() {
    float m = kMultipliers[int(clamp(floor(multiplier + 0.5), 0.0, 4.0))];
    return min(floor(sourcePhase() * m + 1e-4), m - 1.0) / m;
}

// How far the flow's round trip lands from where it started, in pixels, and how badly the two ends
// differ in colour: both are near zero where the motion is real and visible in both frames.
float confidence(vec2 roundTrip, vec2 flow, float colour) {
    float err = length(roundTrip * u_resolution);
    float tol = 1.5 + 0.1 * length(flow * u_resolution);
    return exp(-err * err / (tol * tol)) * exp(-colour * 6.0);
}

// Flow AB: A(x) looks like B(x + AB(x)) with A the shown frame and B the next one. At phase p an object
// that sat at a in A sits at a + p * AB(a), and one that sits at b in B sat at b + (1 - p) * BA(b) when
// the phase was p, so x is looked up in A at the a that solves a + p * AB(a) = x (three fixed-point steps)
// and in B at the b that solves b + (1 - p) * BA(b) = x. A side whose flow does not come back to where it
// started, or whose two ends disagree in colour, is being covered or uncovered there and counts for
// little; with both unreliable the nearer frame is shown as it is.
void main() {
    vec4 shown = texture(u_currentTexture, v_texCoord);
    float p = phase();
    if (p <= 0.0 || futureCount() < 0.5) {
        fragColor = shown;
        return;
    }
    vec2 a = v_texCoord;
    vec2 b = v_texCoord;
    for (int i = 0; i < 3; ++i) {
        a = v_texCoord - p * flowAt(u_texture1, a);
        b = v_texCoord - (1.0 - p) * flowAt(u_texture2, b);
    }
    vec2 fa = flowAt(u_texture1, a);
    vec2 fb = flowAt(u_texture2, b);
    vec2 c = a + fa;
    vec2 d = b + fb;
    vec4 A = texture(u_currentTexture, a);
    vec4 B = futureFrame(1.0, b);
    float colourA = dot(abs(A.rgb - futureFrame(1.0, c).rgb), vec3(0.3333));
    float colourB = dot(abs(B.rgb - texture(u_currentTexture, d).rgb), vec3(0.3333));
    float wA = (1.0 - p) * confidence(fa + flowAt(u_texture2, c), fa, colourA);
    float wB = p * confidence(fb + flowAt(u_texture1, d), fb, colourB);
    float total = wA + wB;
    if (total < 0.02) {
        fragColor = p < 0.5 ? shown : futureFrame(1.0, v_texCoord);
        return;
    }
    fragColor = (A * wA + B * wB) / total;
}
