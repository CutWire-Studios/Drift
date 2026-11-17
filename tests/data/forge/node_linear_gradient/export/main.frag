#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float ang = radians(90.0);
    vec2 d = vec2(cos(ang), sin(ang));
    float t = clamp(dot(uv - 0.5, d) / max(abs(d.x) + abs(d.y), 0.0001) + 0.5, 0.0, 1.0);
    return mix(n_n1_image(uv), vec4(1.0, 1.0, 1.0, 1.0), t);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
