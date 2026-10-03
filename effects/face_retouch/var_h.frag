#version 330 core
// Face Retouch, pass 2: horizontal half of the local variance the smoothing uses to keep edges.
// Ported from GPUPixel's BoxDifferenceFilter (delta 7.07) and BoxBlurFilter,
// Copyright (c) 2021 PixPark, Apache License 2.0. See NOTICE.
// Modified: the squared difference is box-filtered as well, which turns it into a true local
// variance instead of a per-pixel one.
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture; // source
uniform sampler2D u_texture1;       // mean
uniform float u_faceValid; uniform float u_faceHasContours; uniform float u_faceHasMesh;
uniform float u_faceRx;

vec3 diff(vec2 uv) {
    vec3 d = (texture(u_currentTexture, uv).rgb - texture(u_texture1, uv).rgb) * 7.07;
    return min(d * d, 1.0);
}

void main() {
    if (u_faceValid < 0.5 || u_faceHasContours < 0.5 || u_faceHasMesh < 0.5) {
        fragColor = texture(u_currentTexture, v_texCoord);
        return;
    }
    vec2 step = vec2(u_faceRx * 0.02, 0.0);
    vec3 sum = diff(v_texCoord) * (1.0 / 9.0);
    sum += (diff(v_texCoord + step * 1.5) + diff(v_texCoord - step * 1.5)) * (2.0 / 9.0);
    sum += (diff(v_texCoord + step * 3.5) + diff(v_texCoord - step * 3.5)) * (2.0 / 9.0);
    fragColor = vec4(sum, 1.0);
}
