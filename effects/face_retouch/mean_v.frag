#version 330 core
// Face Retouch, pass 1: vertical half of the box mean. See mean_h.frag.
// Ported from GPUPixel's BoxBlurFilter, Copyright (c) 2021 PixPark, Apache License 2.0. See NOTICE.
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture; // mean_h
uniform vec2 u_resolution;
uniform float u_faceValid; uniform float u_faceHasContours; uniform float u_faceHasMesh;
uniform float u_faceRx;

void main() {
    vec4 src = texture(u_currentTexture, v_texCoord);
    if (u_faceValid < 0.5 || u_faceHasContours < 0.5 || u_faceHasMesh < 0.5) { fragColor = src; return; }

    // u_faceRx is width-normalized, so a vertical step in uv is shorter by the aspect.
    float aspect = u_resolution.y / u_resolution.x;
    vec2 step = vec2(0.0, u_faceRx * 0.02 / aspect);
    vec3 sum = src.rgb * (1.0 / 9.0);
    sum += (texture(u_currentTexture, v_texCoord + step * 1.5).rgb
            + texture(u_currentTexture, v_texCoord - step * 1.5).rgb) * (2.0 / 9.0);
    sum += (texture(u_currentTexture, v_texCoord + step * 3.5).rgb
            + texture(u_currentTexture, v_texCoord - step * 3.5).rgb) * (2.0 / 9.0);
    fragColor = vec4(sum, src.a);
}
