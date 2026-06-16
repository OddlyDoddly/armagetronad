#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uModelView;
uniform mat4 uProjection;

out vec2 vWorldXY;

void main() {
    vWorldXY = aPos.xy;
    gl_Position = uProjection * uModelView * vec4(aPos, 1.0);
}
