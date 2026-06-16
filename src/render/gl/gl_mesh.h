/*

*************************************************************************

Retrocycles -- TRON-aesthetic fork of Armagetron Advanced.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de) and contributors.

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

***************************************************************************

*/

#pragma once
// rGL.h → rGLEW.h must be included before checking HAVE_GLEW
#include "rGL.h"
#include <cstddef>

#ifndef DEDICATED
#ifdef HAVE_GLEW

// Vertex layout used for all modern geometry: position (xyz) + texcoord/aux (uv).
struct gl_Vertex {
    float x, y, z;
    float u, v;
};

// Minimal VAO+VBO wrapper. Construct once after GL context exists, destroy before it dies.
class gl_Mesh {
public:
    gl_Mesh() {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(gl_Vertex),
            reinterpret_cast<void*>(offsetof(gl_Vertex, x)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(gl_Vertex),
            reinterpret_cast<void*>(offsetof(gl_Vertex, u)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    ~gl_Mesh() {
        if (vbo_) glDeleteBuffers(1, &vbo_);
        if (vao_) glDeleteVertexArrays(1, &vao_);
    }

    gl_Mesh(const gl_Mesh&) = delete;
    gl_Mesh& operator=(const gl_Mesh&) = delete;

    void upload(const gl_Vertex* data, GLsizei count, GLenum usage = GL_STATIC_DRAW) {
        count_ = count;
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(count) * static_cast<GLsizeiptr>(sizeof(gl_Vertex)),
            data, usage);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void draw(GLenum primitive) const {
        if (!count_) return;
        glBindVertexArray(vao_);
        glDrawArrays(primitive, 0, count_);
        glBindVertexArray(0);
    }

private:
    GLuint vao_ = 0, vbo_ = 0;
    GLsizei count_ = 0;
};

#endif // HAVE_GLEW
#endif // DEDICATED
