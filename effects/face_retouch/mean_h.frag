#version 330 core
// Face Retouch, pass 0: horizontal half of the box mean the smoothing reads.
// Ported from GPUPixel's BoxBlurFilter (radius 4, texel spacing 4),
// Copyright (c) 2021 PixPark, Apache License 2.0. See NOTICE.
// Modified: the tap spacing follows the face size instead of a fixed pixel count.
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float u_faceValid; uniform float u_faceHasContours; uniform float u_faceHasMesh;
uniform float u_faceRx;

void main() {
    vec4 src = texture(u_currentTexture, v_texCoord);
    if (u_faceValid < 0.5 || u_faceHasContours < 0.5 || u_faceHasMesh < 0.5) { fragColor = src; return; }

    // GPUPixel steps 4 pixels at camera resolution, about 2% of a face's half-width. Tying the step
    // to the face rather than to pixels is what keeps a half-size preview and the export alike.
    vec2 step = vec2(u_faceRx * 0.02, 0.0);
    vec3 sum = src.rgb * (1.0 / 9.0);
    sum += (texture(u_currentTexture, v_texCoord + step * 1.5).rgb
            + texture(u_currentTexture, v_texCoord - step * 1.5).rgb) * (2.0 / 9.0);
    sum += (texture(u_currentTexture, v_texCoord + step * 3.5).rgb
            + texture(u_currentTexture, v_texCoord - step * 3.5).rgb) * (2.0 / 9.0);
    fragColor = vec4(sum, src.a);
}
