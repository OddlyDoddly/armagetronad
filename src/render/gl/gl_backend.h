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

#ifndef DEDICATED
#ifdef HAVE_GLEW

// rRender.h must be included after DONTDOIT is optionally defined by the
// including translation unit (gl_backend.cpp defines DONTDOIT so raw
// GL matrix calls are permitted there).
#include "rRender.h"
#include "rShader.h"
#include <vector>
#include <optional>

// Per-vertex data matching batch.vert attribute layout:
//   location 0: vec4 aPos      (xyzw)
//   location 1: vec4 aColor    (rgba)
//   location 2: vec4 aTexCoord (stpq — q for projective division)
struct BatchVertex {
    float x, y, z, w;
    float r, g, b, a;
    float s, t, p, q;
};

namespace gl {

class rGeometryCache;

// Modern VAO/VBO batching renderer.  Implements the full rRenderer interface
// using GLSL shaders instead of fixed-function immediate mode.
//
// Matrix operations still delegate to the GL compatibility matrix stack so
// the rest of the code-base can keep calling glPushMatrix/glTranslatef etc.
// The accumulated matrices are read back via glGetFloatv on each flush().
class ModernGLRenderer : public rRenderer {
public:
    ModernGLRenderer();
    ~ModernGLRenderer() override;

    ModernGLRenderer(const ModernGLRenderer&)            = delete;
    ModernGLRenderer& operator=(const ModernGLRenderer&) = delete;

    // Returns true when GL 3.3 + VAO support is available.
    static bool IsSupported();

    // ---- Geometry cache (display-list replacement) ----

    // While recording, every flush() additionally appends its post-tessellation
    // segment to the given cache (record-and-execute: geometry is still drawn so
    // the first frame is visible).  endRecording() uploads the cache to a static
    // VBO; replayCache() draws it without re-batching on the CPU.
    void beginRecording(rGeometryCache& cache);
    void endRecording();
    void replayCache(const rGeometryCache& cache);

    // ---- rRenderer interface ----

    void Vertex(REAL x, REAL y)                          override;
    void Vertex(REAL x, REAL y, REAL z)                  override;
    void Vertex3(REAL* x)                                override;
    void Vertex(REAL x, REAL y, REAL z, REAL w)          override;

    void TexCoord(REAL u, REAL v)                        override;
    void TexCoord(REAL u, REAL v, REAL w)                override;
    void TexCoord(REAL u, REAL v, REAL w, REAL t)        override;

    void Color(REAL r, REAL g, REAL b)                   override;
    void Color(REAL r, REAL g, REAL b, REAL a)           override;

    void End(bool force = true)                          override;

    void BeginLines()          override;
    void BeginTriangles()      override;
    void BeginQuads()          override;
    void BeginLineStrip()      override;
    void BeginTriangleStrip()  override;
    void BeginQuadStrip()      override;
    void BeginTriangleFan()    override;
    void BeginLineLoop()       override;

    void ProjMatrix()                           override;
    void ModelMatrix()                          override;
    void TexMatrix()                            override;
    void PushMatrix()                           override;
    void PopMatrix()                            override;
    void MultMatrix(REAL mdata[4][4])           override;
    void IdentityMatrix()                       override;
    void ScaleMatrix(REAL f)                    override;
    void ScaleMatrix(REAL f1, REAL f2, REAL f3) override;
    void TranslateMatrix(REAL x1, REAL x2, REAL x3) override;

protected:
    void ReallySetFlag(flag f, bool c) override;

private:
    void flush();
    void beginPrimitive(GLenum prim, bool forceEnd = false);
    void pushVert(float x, float y, float z, float w);
    void ensureShaders();

    float curR_ = 1.f, curG_ = 1.f, curB_ = 1.f, curA_ = 1.f;
    float curS_ = 0.f, curT_ = 0.f, curP_ = 0.f, curQ_ = 1.f;

    GLenum currentPrim_ = GL_FALSE;
    bool   forceEnd_    = false;

    std::vector<BatchVertex> verts_;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;

    std::optional<rShader> colorShader_;
    std::optional<rShader> texturedShader_;

    // Non-null while a geometry cache is being recorded.
    rGeometryCache* recording_ = nullptr;
};

} // namespace gl

#endif // HAVE_GLEW
#endif // DEDICATED
