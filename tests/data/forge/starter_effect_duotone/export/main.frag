#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec3 shadowColor;
uniform vec3 highlightColor;
vec3 pv_shadowColor() { return shadowColor; }
vec3 pv_highlightColor() { return highlightColor; }

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    vec4 col = n_n1_image(uv);
    vec3 rgb = (col.rgb - 0.5) * 1.2 + 0.5 + 0.0;
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}

vec4 n_n3_image(vec2 uv) {
    vec4 col = n_n2_image(uv);
    vec3 duo = mix(vec4(pv_shadowColor(), 1.0).rgb, vec4(pv_highlightColor(), 1.0).rgb, smoothstep(0.0, 1.0, luma(col)));
    return vec4(mix(col.rgb, duo, 1.0), col.a);
}

void main() {
    fragColor = n_n3_image(v_texCoord);
}
