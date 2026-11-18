#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform vec4 k_n2_in2;
uniform float k_n2_a;
uniform float k_n2_b;
uniform float k_n2_c;
uniform float k_n2_d;

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 x_n2_in1(vec2 p) { return n_n1_image(p); }
vec4 x_n2_in2(vec2 p) { return k_n2_in2; }

vec4 n_n2_image(vec2 uv) {
    float a = k_n2_a;
    float b = k_n2_b;
    float c = k_n2_c;
    float d = k_n2_d;
#define IN1 x_n2_in1
#define IN2 x_n2_in2
#define RESOLUTION u_resolution
#define TIME u_time
// Write GLSL that returns a vec4 colour.
// Available: uv (vec2, 0..1, top-left origin), IN1(p) and IN2(p) sample the
// two image inputs at any position, a b c d are the number inputs,
// RESOLUTION is the frame size in pixels, and TIME (effects) or
// PROGRESS (transitions) is the clock.
vec4 col = IN1(uv);
col.rgb = mix(col.rgb, col.bgr, a);
return col;
#undef IN1
#undef IN2
#undef RESOLUTION
#undef TIME
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
