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

#include "aa_config.h"

// DONTDOIT suppresses the glBegin/glEnd/glMatrixMode error-macros in rRender.h
// so we can call the GL compatibility matrix stack from within this renderer.
#define DONTDOIT

#include "gl_backend.h"

#ifndef DEDICATED
#ifdef HAVE_GLEW

#include "tConsole.h"
#include <cstring>  // offsetof

// ---------------------------------------------------------------------------
// GLSL source strings (kept inline; files in shaders/ are the authoritative
// source but embedding avoids runtime file I/O and path resolution).
// ---------------------------------------------------------------------------

static const char kBatchVert[] = R"GLSL(
#version 330 core

layout(location = 0) in vec4 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec4 aTexCoord;

uniform mat4 uModelView;
uniform mat4 uProjection;

out vec4 vColor;
out vec4 vTexCoord;

void main() {
    vColor    = aColor;
    vTexCoord = aTexCoord;
    gl_Position = uProjection * uModelView * aPos;
}
)GLSL";

static const char kBatchColorFrag[] = R"GLSL(
#version 330 core

in vec4 vColor;
in vec4 vTexCoord;

out vec4 fragColor;

void main() {
    fragColor = vColor;
}
)GLSL";

static const char kBatchTexFrag[] = R"GLSL(
#version 330 core

in vec4 vColor;
in vec4 vTexCoord;

uniform sampler2D uTexture;

out vec4 fragColor;

void main() {
    // Project texcoords (q-division), matching GL_MODULATE fixed-function behavior.
    vec2 tc = vTexCoord.xy / max(vTexCoord.w, 0.0001);
    fragColor = texture(uTexture, tc) * vColor;
}
)GLSL";

// ---------------------------------------------------------------------------

namespace gl {

ModernGLRenderer::ModernGLRenderer() {
    // rRenderer base constructor sets renderer = this.
    ChangeFlags(0xffffffff, 0);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    // loc 0: xyzw
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex),
        reinterpret_cast<void*>(offsetof(BatchVertex, x)));
    glEnableVertexAttribArray(0);
    // loc 1: rgba
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex),
        reinterpret_cast<void*>(offsetof(BatchVertex, r)));
    glEnableVertexAttribArray(1);
    // loc 2: stpq
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex),
        reinterpret_cast<void*>(offsetof(BatchVertex, s)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    verts_.reserve(4096);
}

ModernGLRenderer::~ModernGLRenderer() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool ModernGLRenderer::IsSupported() {
    // We're compiled with GLEW, so VAO/VBO/shaders are available.
    return true;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void ModernGLRenderer::ensureShaders() {
    if (!colorShader_) {
        auto r = rShader::create(kBatchVert, kBatchColorFrag);
        if (r) {
            colorShader_ = std::move(*r);
        } else {
            con << "ModernGLRenderer: color shader compile error:\n" << r.error().c_str() << "\n";
        }
    }
    if (!texturedShader_) {
        auto r = rShader::create(kBatchVert, kBatchTexFrag);
        if (r) {
            texturedShader_ = std::move(*r);
        } else {
            con << "ModernGLRenderer: textured shader compile error:\n" << r.error().c_str() << "\n";
        }
    }
}

void ModernGLRenderer::pushVert(float x, float y, float z, float w) {
    verts_.push_back({x, y, z, w,
                      curR_, curG_, curB_, curA_,
                      curS_, curT_, curP_, curQ_});
}

void ModernGLRenderer::beginPrimitive(GLenum prim, bool forceEnd) {
    if (currentPrim_ != prim && currentPrim_ != GL_FALSE)
        flush();
    currentPrim_ = prim;
    forceEnd_    = forceEnd;
}

void ModernGLRenderer::flush() {
    if (verts_.empty()) {
        currentPrim_ = GL_FALSE;
        forceEnd_    = false;
        return;
    }

    ensureShaders();
    if (!colorShader_ || !texturedShader_) {
        verts_.clear();
        currentPrim_ = GL_FALSE;
        return;
    }

    // Read back the accumulated GL compatibility-mode matrices.
    float mv[16], proj[16];
    glGetFloatv(GL_MODELVIEW_MATRIX,  mv);
    glGetFloatv(GL_PROJECTION_MATRIX, proj);

    // Pick shader based on whether a 2D texture is bound.
    GLint boundTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex);
    const rShader& sh = (boundTex != 0) ? *texturedShader_ : *colorShader_;
    sh.use();
    sh.setMatrix4("uModelView",  mv);
    sh.setMatrix4("uProjection", proj);
    if (boundTex != 0)
        glUniform1i(sh.uniformLocation("uTexture"), 0);

    // Tessellate primitive types that don't exist in GL 3.3 Core.
    GLenum drawPrim = currentPrim_;
    const std::vector<BatchVertex>* drawVerts = &verts_;
    std::vector<BatchVertex> tmp;

    if (currentPrim_ == GL_QUADS) {
        drawPrim = GL_TRIANGLES;
        const size_t nQ = verts_.size() / 4;
        tmp.reserve(nQ * 6);
        for (size_t i = 0; i < nQ; ++i) {
            const auto& a = verts_[i*4+0];
            const auto& b = verts_[i*4+1];
            const auto& c = verts_[i*4+2];
            const auto& d = verts_[i*4+3];
            tmp.push_back(a); tmp.push_back(b); tmp.push_back(c);
            tmp.push_back(a); tmp.push_back(c); tmp.push_back(d);
        }
        drawVerts = &tmp;
    } else if (currentPrim_ == GL_QUAD_STRIP) {
        // Pairs of verts form quads: (i, i+1, i+3, i+2) for i=0,2,4,...
        drawPrim = GL_TRIANGLES;
        const size_t n = verts_.size();
        if (n >= 4) {
            tmp.reserve((n / 2 - 1) * 6);
            for (size_t i = 0; i + 3 < n; i += 2) {
                const auto& a = verts_[i+0];
                const auto& b = verts_[i+1];
                const auto& c = verts_[i+2];
                const auto& d = verts_[i+3];
                tmp.push_back(a); tmp.push_back(b); tmp.push_back(d);
                tmp.push_back(a); tmp.push_back(d); tmp.push_back(c);
            }
        }
        drawVerts = &tmp;
    } else if (currentPrim_ == GL_LINE_LOOP) {
        // Render as line strip with closing segment.
        drawPrim = GL_LINE_STRIP;
        tmp = verts_;
        tmp.push_back(verts_[0]);
        drawVerts = &tmp;
    }

    if (!drawVerts->empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(drawVerts->size()) * static_cast<GLsizeiptr>(sizeof(BatchVertex)),
            drawVerts->data(), GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glBindVertexArray(vao_);
        glDrawArrays(drawPrim, 0, static_cast<GLsizei>(drawVerts->size()));
        glBindVertexArray(0);
    }

    verts_.clear();
    currentPrim_ = GL_FALSE;
    forceEnd_    = false;

    // Restore program 0 so fixed-function paths still work if mixed.
    glUseProgram(0);
}

// ---------------------------------------------------------------------------
// Vertex / TexCoord / Color
// ---------------------------------------------------------------------------

void ModernGLRenderer::Vertex(REAL x, REAL y)                    { pushVert(x, y, 0.f, 1.f); }
void ModernGLRenderer::Vertex(REAL x, REAL y, REAL z)            { pushVert(x, y, z,   1.f); }
void ModernGLRenderer::Vertex3(REAL* x)                          { pushVert(x[0], x[1], x[2], 1.f); }
void ModernGLRenderer::Vertex(REAL x, REAL y, REAL z, REAL w)    { pushVert(x, y, z, w); }

void ModernGLRenderer::TexCoord(REAL u, REAL v) {
    curS_ = u; curT_ = v; curP_ = 0.f; curQ_ = 1.f;
}
void ModernGLRenderer::TexCoord(REAL u, REAL v, REAL w) {
    curS_ = u; curT_ = v; curP_ = w; curQ_ = 1.f;
}
void ModernGLRenderer::TexCoord(REAL u, REAL v, REAL w, REAL t) {
    curS_ = u; curT_ = v; curP_ = w; curQ_ = t;
}

void ModernGLRenderer::Color(REAL r, REAL g, REAL b) {
    curR_ = r; curG_ = g; curB_ = b; curA_ = 1.f;
}
void ModernGLRenderer::Color(REAL r, REAL g, REAL b, REAL a) {
    curR_ = r; curG_ = g; curB_ = b; curA_ = a;
}

// ---------------------------------------------------------------------------
// Primitive control
// ---------------------------------------------------------------------------

void ModernGLRenderer::End(bool force) {
    if ((forceEnd_ || force) && currentPrim_ != GL_FALSE)
        flush();
}

void ModernGLRenderer::BeginLines()         { beginPrimitive(GL_LINES);          }
void ModernGLRenderer::BeginTriangles()     { beginPrimitive(GL_TRIANGLES);      }
void ModernGLRenderer::BeginQuads()         { beginPrimitive(GL_QUADS);          }
void ModernGLRenderer::BeginLineStrip()     { beginPrimitive(GL_LINE_STRIP,  true); }
void ModernGLRenderer::BeginLineLoop()      { beginPrimitive(GL_LINE_LOOP,   true); }
void ModernGLRenderer::BeginTriangleStrip() { beginPrimitive(GL_TRIANGLE_STRIP, true); }
void ModernGLRenderer::BeginQuadStrip()     { beginPrimitive(GL_QUAD_STRIP,  true); }
void ModernGLRenderer::BeginTriangleFan()   { beginPrimitive(GL_TRIANGLE_FAN, true); }

// ---------------------------------------------------------------------------
// Matrix operations — delegate to GL compatibility matrix stack so that
// the rest of the code-base can continue to call TranslateMatrix etc.
// The accumulated matrices are read back via glGetFloatv in flush().
// ---------------------------------------------------------------------------

void ModernGLRenderer::ProjMatrix()  { End(true); glMatrixMode(GL_PROJECTION); }
void ModernGLRenderer::ModelMatrix() { End(true); glMatrixMode(GL_MODELVIEW);  }
void ModernGLRenderer::TexMatrix()   { End(true); glMatrixMode(GL_TEXTURE);    }

void ModernGLRenderer::PushMatrix()  { glPushMatrix(); }
void ModernGLRenderer::PopMatrix()   { End(true); glPopMatrix(); }

void ModernGLRenderer::MultMatrix(REAL mdata[4][4]) {
    End(true);
    static_assert(sizeof(REAL) == sizeof(GLfloat), "REAL must be float");
    glMultMatrixf(reinterpret_cast<GLfloat*>(&mdata[0][0]));
}

void ModernGLRenderer::IdentityMatrix()                            { End(true); glLoadIdentity(); }
void ModernGLRenderer::ScaleMatrix(REAL f)                        { End(true); glScalef(f, f, f); }
void ModernGLRenderer::ScaleMatrix(REAL f1, REAL f2, REAL f3)    { End(true); glScalef(f1, f2, f3); }
void ModernGLRenderer::TranslateMatrix(REAL x1, REAL x2, REAL x3){ End(true); glTranslatef(x1, x2, x3); }

// ---------------------------------------------------------------------------
// Render state flags
// ---------------------------------------------------------------------------

void ModernGLRenderer::ReallySetFlag(flag f, bool c) {
    GLenum fl = GL_DEPTH_TEST;
    switch (f) {
    case ALPHA_BLEND: fl = GL_BLEND;      break;
    case DEPTH_TEST:  fl = GL_DEPTH_TEST; break;
    default: return;
    }
    if (c) glEnable(fl); else glDisable(fl);
}

} // namespace gl

#endif // HAVE_GLEW
#endif // DEDICATED
