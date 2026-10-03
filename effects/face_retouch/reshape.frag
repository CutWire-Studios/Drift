#version 330 core
// Face Retouch, pass 7: slim face and big eyes. Runs last so the makeup moves with the face.
// Ported from GPUPixel's FaceReshapeFilter, Copyright (c) 2021 PixPark, Apache License 2.0.
// See NOTICE. Modified: driven by Drift's 111 points, which are already width-normalized, so
// GPUPixel's aspect correction is the identity here.
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture; // skin
uniform vec2 u_resolution;
uniform float u_faceValid; uniform float u_faceHasContours; uniform float u_faceHasMesh;
uniform vec2 u_faceLandmarks111[111];
uniform float slimFace; uniform float bigEyes;

vec2 enlargeEye(vec2 p, vec2 origin, float radius, float delta) {
    float weight = distance(p, origin) / radius;
    weight = clamp(1.0 - (1.0 - weight * weight) * delta, 0.0, 1.0);
    return origin + (p - origin) * weight;
}

vec2 curveWarp(vec2 p, vec2 origin, vec2 target, float delta) {
    vec2 direction = (target - origin) * delta;
    float radius = distance(target, origin);
    float ratio = clamp(1.0 - distance(p, origin) / radius, 0.0, 1.0);
    return p - direction * ratio;
}

void main() {
    if (u_faceValid < 0.5 || u_faceHasContours < 0.5 || u_faceHasMesh < 0.5
        || (slimFace <= 0.0 && bigEyes <= 0.0)) {
        fragColor = texture(u_currentTexture, v_texCoord);
        return;
    }

    float aspect = u_resolution.y / u_resolution.x;
    vec2 p = vec2(v_texCoord.x, v_texCoord.y * aspect);

    // Jaw points pulled toward the nose line: pairs of (jaw, nose) indices.
    const ivec2 kThin[9] = ivec2[9](ivec2(3, 44), ivec2(29, 44), ivec2(7, 45), ivec2(25, 45),
                                    ivec2(10, 46), ivec2(22, 46), ivec2(14, 49), ivec2(18, 49),
                                    ivec2(16, 49));
    float thinDelta = slimFace * 0.05;
    for (int i = 0; i < 9; i++)
        p = curveWarp(p, u_faceLandmarks111[kThin[i].x], u_faceLandmarks111[kThin[i].y], thinDelta);

    // Eye centres 74 and 77, sized off the upper lid (72 and 75).
    float eyeDelta = bigEyes * 0.15;
    p = enlargeEye(p, u_faceLandmarks111[74],
                   5.0 * distance(u_faceLandmarks111[72], u_faceLandmarks111[74]), eyeDelta);
    p = enlargeEye(p, u_faceLandmarks111[77],
                   5.0 * distance(u_faceLandmarks111[75], u_faceLandmarks111[77]), eyeDelta);

    fragColor = texture(u_currentTexture, clamp(vec2(p.x, p.y / aspect), 0.0, 1.0));
}
