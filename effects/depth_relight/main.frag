#version 330 core
// Relights the frame from the clip's depth. Every pixel is placed in a pseudo camera space —
// x across the frame (scaled by aspect), y down it, z away from the camera — using depth for z,
// and lit by up to four point or spot lights against normals taken from the depth gradients.
//
// The frame's colour stands in for albedo, so a light can add to what is there but not take
// away light the footage was shot with; "Existing light" dims that to let the new lights read.
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;

uniform float ambient;
uniform float relief;
uniform float depthRange;
uniform float specular;
uniform float shininess;
uniform float shadows;

uniform float light1_enabled; uniform float light1_x; uniform float light1_y;
uniform float light1_z; uniform vec3 light1_color; uniform float light1_intensity;
uniform float light1_radius; uniform float light1_cone; uniform float light1_aimX;
uniform float light1_aimY;
uniform float light2_enabled; uniform float light2_x; uniform float light2_y;
uniform float light2_z; uniform vec3 light2_color; uniform float light2_intensity;
uniform float light2_radius; uniform float light2_cone; uniform float light2_aimX;
uniform float light2_aimY;
uniform float light3_enabled; uniform float light3_x; uniform float light3_y;
uniform float light3_z; uniform vec3 light3_color; uniform float light3_intensity;
uniform float light3_radius; uniform float light3_cone; uniform float light3_aimX;
uniform float light3_aimY;
uniform float light4_enabled; uniform float light4_x; uniform float light4_y;
uniform float light4_z; uniform vec3 light4_color; uniform float light4_intensity;
uniform float light4_radius; uniform float light4_cone; uniform float light4_aimX;
uniform float light4_aimY;

float aspect() { return u_resolution.x / max(u_resolution.y, 1.0); }

// Scene depth at uv in camera space: 0 at the nearest thing in the clip, depthRange at the
// farthest.
float sceneZ(vec2 uv) { return (1.0 - driftDepth(uv)) * depthRange; }

vec3 toCamera(vec2 uv, float z) { return vec3((uv.x - 0.5) * aspect(), uv.y - 0.5, z); }
vec2 toUv(vec3 p) { return vec2(p.x / aspect() + 0.5, p.y + 0.5); }

// Fraction of the light reaching p, by marching towards it and checking whether the depth map
// has a surface nearer the camera than the ray anywhere on the way.
float visibility(vec3 p, vec3 lightPos)
{
    if (shadows <= 0.0)
        return 1.0;
    const int kSteps = 20;
    float blocked = 0.0;
    float bias = 0.02 * depthRange;
    for (int i = 1; i < kSteps; ++i) {
        vec3 q = mix(p, lightPos, float(i) / float(kSteps));
        vec2 uv = toUv(q);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            break;
        blocked = max(blocked, smoothstep(bias, bias * 4.0, q.z - sceneZ(uv)));
    }
    return 1.0 - blocked * shadows;
}

void shade(vec3 p, vec3 n, float enabled, float lx, float ly, float lz, vec3 color,
           float intensity, float radius, float cone, float aimX, float aimY,
           inout vec3 diffuse, inout vec3 shine)
{
    if (enabled < 0.5 || intensity <= 0.0)
        return;
    vec3 lightPos = toCamera(vec2(lx, ly), lz * depthRange);
    vec3 toLight = lightPos - p;
    float dist = length(toLight);
    vec3 l = toLight / max(dist, 1e-4);
    float falloff = 1.0 / (1.0 + (dist * dist) / (radius * radius));

    float spot = 1.0;
    if (cone < 179.5) {
        vec2 aimUv = vec2(aimX, aimY);
        vec3 axis = normalize(toCamera(aimUv, sceneZ(aimUv)) - lightPos);
        float outer = cos(radians(cone * 0.5));
        float inner = cos(radians(cone * 0.5 * 0.75));
        spot = smoothstep(outer, inner, dot(-l, axis));
    }
    if (spot <= 0.0)
        return;

    float lambert = max(dot(n, l), 0.0);
    if (lambert <= 0.0)
        return;
    float amount = intensity * falloff * spot * visibility(p, lightPos);
    diffuse += color * lambert * amount;
    // Orthographic viewer: the camera looks along +z, so the direction to it is -z.
    vec3 h = normalize(l + vec3(0.0, 0.0, -1.0));
    shine += color * pow(max(dot(n, h), 0.0), shininess) * amount;
}

void main()
{
    vec4 src = texture(u_currentTexture, v_texCoord);
    if (u_hasDepth < 0.5) {
        fragColor = src;
        return;
    }

    float d = driftDepthGuided(v_texCoord, u_currentTexture);
    vec3 p = toCamera(v_texCoord, (1.0 - d) * depthRange);
    // driftNormal points towards the viewer along +z; camera space runs the other way.
    vec3 un = driftNormal(v_texCoord, relief * depthRange);
    // A depth jump is where one thing passes in front of another, not a surface turned edge-on:
    // left alone its normal faces sideways and draws a dark outline round every silhouette.
    // Capping the tilt at about 50 degrees keeps real curvature and drops the outlines.
    const float kMaxTilt = 1.2;
    float tilt = length(un.xy) / max(un.z, 1e-3);
    if (tilt > kMaxTilt)
        un = normalize(vec3(un.xy / length(un.xy) * kMaxTilt, 1.0));
    vec3 n = normalize(vec3(un.xy, -un.z));

    vec3 diffuse = vec3(0.0);
    vec3 shine = vec3(0.0);
    shade(p, n, light1_enabled, light1_x, light1_y, light1_z, light1_color, light1_intensity,
          light1_radius, light1_cone, light1_aimX, light1_aimY, diffuse, shine);
    shade(p, n, light2_enabled, light2_x, light2_y, light2_z, light2_color, light2_intensity,
          light2_radius, light2_cone, light2_aimX, light2_aimY, diffuse, shine);
    shade(p, n, light3_enabled, light3_x, light3_y, light3_z, light3_color, light3_intensity,
          light3_radius, light3_cone, light3_aimX, light3_aimY, diffuse, shine);
    shade(p, n, light4_enabled, light4_x, light4_y, light4_z, light4_color, light4_intensity,
          light4_radius, light4_cone, light4_aimX, light4_aimY, diffuse, shine);

    vec3 lit = src.rgb * (ambient + diffuse) + shine * specular;
    fragColor = vec4(lit, src.a);
}
