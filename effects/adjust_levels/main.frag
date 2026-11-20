#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float input_start;
uniform float input_end;
uniform float gamma;
uniform float output_start;
uniform float output_end;

void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    float inS = clamp(input_start, 0.0, 0.999);
    float inE = clamp(input_end, inS + 0.001, 1.0);
    vec3 rgb = clamp((c.rgb - vec3(inS)) / (inE - inS), 0.0, 1.0);
    float g = max(gamma, 0.001);
    rgb = pow(rgb, vec3(1.0 / g));
    float outS = clamp(output_start, 0.0, 1.0);
    float outE = clamp(output_end, 0.0, 1.0);
    rgb = mix(vec3(outS), vec3(outE), rgb);
    fragColor = vec4(clamp(rgb, 0.0, 1.0), c.a);
}
