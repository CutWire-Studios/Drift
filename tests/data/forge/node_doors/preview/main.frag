#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float k_n2_perspective;

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

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n4_image(vec2 uv) {
    return texture(u_texture1, uv);
}

vec4 n_n2_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    float k = max(k_n2_perspective, 0.05);
    vec3 rd = vec3((uv - 0.5) * vec2(asp, 1.0) * k, 1.0);
    float D = 1.0 / k;
    float p = clamp(u_progress, 0.0, 1.0);
    float a = p * 1.5707963;
    mat3 RL = rotY(a);
    mat3 RR = rotY(-a);
    vec3 hingeL = vec3(-asp * 0.5, 0.0, D);
    vec3 hingeR = vec3(asp * 0.5, 0.0, D);
    vec4 hl = rayQuad(rd, hingeL + RL * vec3(asp * 0.25, 0.0, 0.0), RL * vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), asp * 0.5, 1.0);
    vec4 hr = rayQuad(rd, hingeR + RR * vec3(-asp * 0.25, 0.0, 0.0), RR * vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), asp * 0.5, 1.0);
    bool okL = hl.z > 0.0;
    bool okR = hr.z > 0.0;
    float shade = 1.0 - 0.45 * sin(a);
    if (okL && (!okR || hl.z <= hr.z)) {
        vec4 col = n_n1_image(vec2(hl.x * 0.5, hl.y));
        return vec4(col.rgb * shade, col.a);
    }
    if (okR) {
        vec4 col = n_n1_image(vec2(0.5 + hr.x * 0.5, hr.y));
        return vec4(col.rgb * shade, col.a);
    }
    return n_n4_image((uv - 0.5) / (0.85 + 0.15 * p) + 0.5);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
