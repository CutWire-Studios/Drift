#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 x_n2_in1(vec2 p) { return n_n1_image(p); }
vec4 x_n2_in2(vec2 p) { return vec4(0.0, 0.0, 0.0, 1.0); }

vec4 n_n2_image(vec2 uv) {
    float a = 0.5;
    float b = 0.0;
    float c = 0.0;
    float d = 0.0;
#define IN1 x_n2_in1
#define IN2 x_n2_in2
#define RESOLUTION u_resolution
#define TIME u_time
vec4 col = IN1(uv + vec2(a * 0.05, 0.0));
col.rgb = mix(col.rgb, vec3(luma(col)), b);
return col;
#undef IN1
#undef IN2
#undef RESOLUTION
#undef TIME
}

void main() {
    fragColor = n_n2_image(v_texCoord);
}
