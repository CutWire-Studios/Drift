#version 330 core
// Face Retouch, pass 5: lipstick, drawn over GPUPixel's 111-point face mesh ("geometry": "face111").
// Ported from GPUPixel's FaceMakeupFilter (multiply blend) and mouth.png,
// Copyright (c) 2021 PixPark, Apache License 2.0. See NOTICE. Modified: the template is converted to
// straight alpha, cut off at its own edges, and a custom colour can replace its tint.
in vec2 v_texCoord;      // screen uv
in vec2 v_templateCoord; // uv in mouth.png
out vec4 fragColor;
uniform sampler2D u_currentTexture; // the frame so far
uniform sampler2D u_texture1;       // mouth.png, straight alpha (tools/convert_makeup_templates.py)
uniform float u_faceValid; uniform float u_faceHasContours; uniform float u_faceHasMesh;
uniform float lipstick; uniform float lipCustom; uniform vec3 lipColor;

void main() {
    vec4 bg = texture(u_currentTexture, v_texCoord);
    if (u_faceValid < 0.5 || u_faceHasContours < 0.5 || u_faceHasMesh < 0.5 || lipstick <= 0.0) {
        fragColor = bg;
        return;
    }

    // Package textures wrap, and the mesh reaches well past the template, so anything outside
    // [0, 1] is zero and the rest stays half a texel in.
    vec2 t = v_templateCoord;
    if (any(lessThan(t, vec2(0.0))) || any(greaterThan(t, vec2(1.0)))) { fragColor = bg; return; }
    vec2 halfTexel = 0.5 / vec2(textureSize(u_texture1, 0));
    vec4 tpl = texture(u_texture1, clamp(t, halfTexel, 1.0 - halfTexel));

    // The alpha carries the painted shading, so a custom colour keeps the template's look.
    vec3 col = lipCustom > 0.5 ? lipColor : tpl.rgb;
    fragColor = vec4(mix(bg.rgb, bg.rgb * clamp(col, 0.0, 1.0), tpl.a * lipstick), bg.a);
}
