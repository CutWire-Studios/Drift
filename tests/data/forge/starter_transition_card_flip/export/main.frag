#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float u_progress;

// Camera at the origin looking down +z with y pointing down. Intersects ray rd with a w-by-h
// quad centred at c whose unit axes are ax and ay. Returns the hit's 0..1 position on the quad
// (.xy), its distance (.z, negative for a miss) and 1.0 in .w when the front face is visible.
vec4 rayQuad(vec3 rd, vec3 c, vec3 ax, vec3 ay, float w, float h) {
    vec3 n = cross(ax, ay);
    float den = dot(rd, n);
    if (abs(den) < 1e-6) return vec4(0.0, 0.0, -1.0, 0.0);
    float t = dot(c, n) / den;
    if (t <= 0.0) return vec4(0.0, 0.0, -1.0, 0.0);
    vec3 hit = rd * t - c;
    vec2 q = vec2(dot(hit, ax) / w, dot(hit, ay) / h) + 0.5;
    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) return vec4(q, -1.0, 0.0);
    return vec4(q, t, den > 0.0 ? 1.0 : 0.0);
}

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

float n_n3_value(vec2 uv) {
    return u_progress;
}

float n_n4_value(vec2 uv) {
    float t = clamp(n_n3_value(uv), 0.0, 1.0);
    return t < 0.5 ? 4.0 * t * t * t : 1.0 - pow(-2.0 * t + 2.0, 3.0) / 2.0;
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return texture(u_texture1, uv);
}

vec4 n_n5_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    float k = max(1.0, 0.05);
    vec3 rd = vec3((uv - 0.5) * vec2(asp, 1.0) * k, 1.0);
    float D = 1.0 / k;
    float p = clamp(n_n4_value(uv), 0.0, 1.0);
    float a = p * 3.14159265;
    mat3 R = rotY(a);
    vec3 ctr = vec3(0.0, 0.0, D + 0.4 * sin(p * 3.14159265));
    vec4 h = rayQuad(rd, ctr, R * vec3(1.0, 0.0, 0.0), R * vec3(0.0, 1.0, 0.0), asp, 1.0);
    if (h.z < 0.0) return vec4(0.0);
    if (h.w > 0.5) return n_n1_image(h.xy);
    return n_n2_image(vec2(1.0 - h.x, h.y));
}

void main() {
    fragColor = n_n5_image(v_texCoord);
}
