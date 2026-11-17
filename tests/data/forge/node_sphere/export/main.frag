#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

mat3 rotX(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat3(1.0, 0.0, 0.0, 0.0, c, s, 0.0, -s, c);
}

mat3 rotY(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c);
}

mat3 rotZ(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat3(c, s, 0.0, -s, c, 0.0, 0.0, 0.0, 1.0);
}

// Degrees around X, then Y, then Z.
mat3 rot3(vec3 deg) {
    vec3 r = radians(deg);
    return rotZ(r.z) * rotY(r.y) * rotX(r.x);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 d = (uv - vec2(0.5, 0.5)) * vec2(asp, 1.0) / max(0.42, 0.0001);
    float r2 = dot(d, d);
    if (r2 > 1.0) return vec4(0.0);
    vec3 n = vec3(d, -sqrt(1.0 - r2));
    vec3 s = rot3(vec3(10.0, 0.0, 0.0)) * n;
    float lon = atan(s.x, -s.z) / 6.2831853 + 0.5;
    float lat = asin(clamp(s.y, -1.0, 1.0)) / 3.14159265 + 0.5;
    vec4 col = n_n1_image(vec2(lon, lat));
    float light = clamp(dot(n, normalize(vec3(-0.4, -0.5, -0.75))), 0.0, 1.0);
    col.rgb *= mix(1.0, 0.25 + 0.85 * light, 0.6);
    float edge = 1.0 - smoothstep(0.97, 1.0, r2);
    return vec4(col.rgb, col.a * edge);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
