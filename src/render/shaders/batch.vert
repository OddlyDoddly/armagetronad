#version 330 core

layout(location = 0) in vec4 aPos;      // xyz + w (w=0 = point at infinity)
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec4 aTexCoord; // stpq (q for projective division)

uniform mat4 uModelView;
uniform mat4 uProjection;

out vec4 vColor;
out vec4 vTexCoord;

void main() {
    vColor    = aColor;
    vTexCoord = aTexCoord;
    gl_Position = uProjection * uModelView * aPos;
}
