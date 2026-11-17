#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_faceLeftEyeX;
uniform float u_faceLeftEyeY;
uniform float u_faceRightEyeX;
uniform float u_faceRightEyeY;
uniform float u_faceNoseX;
uniform float u_faceNoseY;
uniform float u_faceMouthX;
uniform float u_faceMouthY;
uniform float u_faceChinX;
uniform float u_faceChinY;
uniform float u_faceForeheadX;
uniform float u_faceForeheadY;
uniform float u_faceCenterX;
uniform float u_faceCenterY;
uniform float u_faceAngle;
uniform float u_faceRx;
uniform float u_faceRy;
uniform float u_faceValid;

float n_n2_angle(vec2 uv) {
    return u_faceAngle;
}

void main() {
    if (u_faceValid < 0.5) {
        fragColor = texture(u_currentTexture, v_texCoord);
        return;
    }
    fragColor = vec4(vec3(n_n2_angle(v_texCoord)), 1.0);
}
