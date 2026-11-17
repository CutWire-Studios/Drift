#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float k_n2_angle;
uniform float k_n2_radius;
uniform float k_n2_shadow;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n4_image(vec2 uv) {
    return texture(u_texture1, uv);
}

vec4 n_n2_image(vec2 uv) {
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 q = uv * vec2(asp, 1.0);
    float ang = radians(k_n2_angle);
    // The curl travels along -d; paper beyond the axis (x > A) is rolled around a cylinder.
    vec2 d = -vec2(cos(ang), sin(ang));
    float r = k_n2_radius;
    float x0 = dot(vec2(0.0), d);
    float x1 = dot(vec2(asp, 0.0), d);
    float x2 = dot(vec2(0.0, 1.0), d);
    float x3 = dot(vec2(asp, 1.0), d);
    float xmin = min(min(x0, x1), min(x2, x3));
    float xmax = max(max(x0, x1), max(x2, x3));
    float A = mix(xmax, xmin - 3.14159265 * r, clamp(u_progress, 0.0, 1.0));
    float x = dot(q, d);
    vec2 perp = q - d * x;
    vec2 pg;
    if (x <= A) {
        pg = (perp + d * (2.0 * A + 3.14159265 * r - x)) / vec2(asp, 1.0);
        if (pg.x >= 0.0 && pg.x <= 1.0 && pg.y >= 0.0 && pg.y <= 1.0) {
            return vec4(mix(n_n1_image(pg).rgb, vec3(0.92), 0.75), 1.0);
        }
        return n_n1_image(uv);
    }
    if (x <= A + r) {
        float th = asin(clamp((x - A) / r, 0.0, 1.0));
        pg = (perp + d * (A + r * (3.14159265 - th))) / vec2(asp, 1.0);
        if (pg.x >= 0.0 && pg.x <= 1.0 && pg.y >= 0.0 && pg.y <= 1.0) {
            return vec4(mix(n_n1_image(pg).rgb, vec3(0.92), 0.75) * (0.75 + 0.25 * cos(th)), 1.0);
        }
        pg = (perp + d * (A + r * th)) / vec2(asp, 1.0);
        if (pg.x >= 0.0 && pg.x <= 1.0 && pg.y >= 0.0 && pg.y <= 1.0) {
            vec4 col = n_n1_image(pg);
            return vec4(col.rgb * (1.0 - 0.35 * sin(th)), col.a);
        }
    }
    vec4 under = n_n4_image(uv);
    float sh = (1.0 - smoothstep(0.0, r * 1.5, x - A - r)) * k_n2_shadow * step(A + r, x) * 0.6;
    return vec4(under.rgb * (1.0 - sh), under.a);
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
