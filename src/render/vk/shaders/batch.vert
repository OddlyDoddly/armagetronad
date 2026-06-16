#version 450

layout(location = 0) in vec4 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;

layout(push_constant) uniform Push {
    mat4 mvp;
} pc;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexCoord;

void main() {
    vColor      = aColor;
    vTexCoord   = aTexCoord;
    gl_Position = pc.mvp * aPos;
}
