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

#include "gl_geometry_cache.h"
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
layout(location = 3) in vec3 aNormal;

uniform mat4 uModelView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out vec4 vColor;
out vec4 vTexCoord;
out vec3 vEyePos;
out vec3 vNormal;

void main() {
    vColor    = aColor;
    vTexCoord = aTexCoord;
    vec4 eye  = uModelView * aPos;
    vEyePos   = eye.xyz;
    vNormal   = uNormalMatrix * aNormal;
    gl_Position = uProjection * eye;
}
)GLSL";

// Shared lighting block: two lights, diffuse + (white) specular, computed in
// eye space.  Approximates the fixed-function two-light setup the cycle model
// used (glLightfv).  Disabled lights contribute nothing.
static const char kLightingGLSL[] = R"GLSL(
uniform int  uLightingEnabled;
uniform vec4 uLightPos[2];   // eye space; w==0 => directional
uniform vec3 uLightColor[2]; // zero when the light is disabled

vec3 applyLighting(vec3 base) {
    if (uLightingEnabled == 0)
        return base;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(-vEyePos);
    vec3 lit = base * 0.25; // ambient term so unlit faces aren't black

    for (int i = 0; i < 2; ++i) {
        vec3 L = (uLightPos[i].w == 0.0)
               ? normalize(uLightPos[i].xyz)
               : normalize(uLightPos[i].xyz - vEyePos);
        float ndl = max(dot(N, L), 0.0);
        lit += base * uLightColor[i] * ndl;

        vec3 H = normalize(L + V);
        float ndh = max(dot(N, H), 0.0);
        lit += uLightColor[i] * pow(ndh, 16.0);
    }
    return lit;
}
)GLSL";

static const char kBatchColorFrag[] = R"GLSL(
#version 330 core

in vec4 vColor;
in vec4 vTexCoord;
in vec3 vEyePos;
in vec3 vNormal;

out vec4 fragColor;
)GLSL"
// lighting helper appended below
;

static const char kBatchColorFragMain[] = R"GLSL(
void main() {
    fragColor = vec4(applyLighting(vColor.rgb), vColor.a);
}
)GLSL";

static const char kBatchTexFrag[] = R"GLSL(
#version 330 core

in vec4 vColor;
in vec4 vTexCoord;
in vec3 vEyePos;
in vec3 vNormal;

uniform sampler2D uTexture;

out vec4 fragColor;
)GLSL"
;

static const char kBatchTexFragMain[] = R"GLSL(
void main() {
    // Project texcoords (q-division), matching GL_MODULATE fixed-function behavior.
    vec2 tc = vTexCoord.xy / max(vTexCoord.w, 0.0001);
    vec4 base = texture(uTexture, tc) * vColor;
    fragColor = vec4(applyLighting(base.rgb), base.a);
}
)GLSL";

// ---------------------------------------------------------------------------

// Normal matrix = inverse-transpose of the upper-left 3x3 of the (column-major)
// model-view matrix, written column-major for glUniformMatrix3fv.
static void computeNormalMatrix(const float mv[16], float out[9]) {
    const float a = mv[0], b = mv[1], c = mv[2];
    const float d = mv[4], e = mv[5], f = mv[6];
    const float g = mv[8], h = mv[9], i = mv[10];

    const float A =  (e*i - f*h);
    const float B = -(d*i - f*g);
    const float C =  (d*h - e*g);
    float det = a*A + b*B + c*C;

    if (det == 0.f) {
        // Degenerate: fall back to the plain rotation/scale part.
        out[0]=a; out[1]=b; out[2]=c;
        out[3]=d; out[4]=e; out[5]=f;
        out[6]=g; out[7]=h; out[8]=i;
        return;
    }
    const float invDet = 1.f / det;

    // inverse = adjugate / det ; normal matrix = transpose(inverse).
    // Columns of the transpose-of-inverse, laid out column-major.
    out[0] = A * invDet;
    out[1] = B * invDet;
    out[2] = C * invDet;
    out[3] = -(b*i - c*h) * invDet;
    out[4] =  (a*i - c*g) * invDet;
    out[5] = -(a*h - b*g) * invDet;
    out[6] =  (b*f - c*e) * invDet;
    out[7] = -(a*f - c*d) * invDet;
    out[8] =  (a*e - b*d) * invDet;
}

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
    // loc 3: normal xyz
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(BatchVertex),
        reinterpret_cast<void*>(offsetof(BatchVertex, nx)));
    glEnableVertexAttribArray(3);

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
        std::string frag = std::string(kBatchColorFrag) + kLightingGLSL + kBatchColorFragMain;
        auto r = rShader::create(kBatchVert, frag.c_str());
        if (r) {
            colorShader_ = std::move(*r);
        } else {
            con << "ModernGLRenderer: color shader compile error:\n" << r.error().c_str() << "\n";
        }
    }
    if (!texturedShader_) {
        std::string frag = std::string(kBatchTexFrag) + kLightingGLSL + kBatchTexFragMain;
        auto r = rShader::create(kBatchVert, frag.c_str());
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
                      curS_, curT_, curP_, curQ_,
                      curNx_, curNy_, curNz_});
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
    float nrm[9];
    computeNormalMatrix(mv, nrm);
    GLint nrmLoc = sh.uniformLocation("uNormalMatrix");
    if (nrmLoc >= 0)
        glUniformMatrix3fv(nrmLoc, 1, GL_FALSE, nrm);
    applyLighting(sh);
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
        // Record-and-execute: while a display list is recording, capture this
        // segment (geometry + the GL state needed to reproduce it) so it can be
        // replayed from a static VBO on subsequent frames.
        if (recording_) {
            recording_->append(drawPrim, drawVerts->data(), drawVerts->size(),
                static_cast<GLuint>(boundTex),
                glIsEnabled(GL_BLEND)                 == GL_TRUE,
                glIsEnabled(GL_DEPTH_TEST)            == GL_TRUE,
                glIsEnabled(GL_POLYGON_OFFSET_FILL)   == GL_TRUE,
                lightingEnabled_);
        }

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

void ModernGLRenderer::Normal(REAL x, REAL y, REAL z) {
    curNx_ = x; curNy_ = y; curNz_ = z;
}

void ModernGLRenderer::Color(REAL r, REAL g, REAL b) {
    curR_ = r; curG_ = g; curB_ = b; curA_ = 1.f;
}
void ModernGLRenderer::Color(REAL r, REAL g, REAL b, REAL a) {
    curR_ = r; curG_ = g; curB_ = b; curA_ = a;
}

// ---------------------------------------------------------------------------
// Lighting
// ---------------------------------------------------------------------------

void ModernGLRenderer::Lighting(bool on) {
    // A change in lighting state must not retroactively affect already-batched
    // geometry, so flush first.
    if (lightingEnabled_ != on)
        End(true);
    lightingEnabled_ = on;
}

void ModernGLRenderer::Light(int index, bool enabled,
                             REAL x, REAL y, REAL z, REAL w,
                             REAL r, REAL g, REAL b) {
    if (index < 0 || index >= kMaxLights)
        return;

    End(true);

    LightState& L = lights_[index];
    L.enabled = enabled;

    // Transform the position/direction into eye space with the current
    // model-view matrix, exactly as glLightfv(GL_POSITION) does.
    float mv[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    const float in[4] = { float(x), float(y), float(z), float(w) };
    for (int row = 0; row < 4; ++row) {
        // column-major mv: element(row, col) = mv[col*4 + row]
        L.pos[row] = mv[0*4 + row] * in[0] + mv[1*4 + row] * in[1]
                   + mv[2*4 + row] * in[2] + mv[3*4 + row] * in[3];
    }

    L.color[0] = enabled ? float(r) : 0.f;
    L.color[1] = enabled ? float(g) : 0.f;
    L.color[2] = enabled ? float(b) : 0.f;
}

void ModernGLRenderer::applyLighting(const rShader& sh) const {
    glUniform1i(sh.uniformLocation("uLightingEnabled"), lightingEnabled_ ? 1 : 0);

    GLint posLoc   = sh.uniformLocation("uLightPos");
    GLint colorLoc = sh.uniformLocation("uLightColor");
    float pos[kMaxLights * 4];
    float col[kMaxLights * 3];
    for (int i = 0; i < kMaxLights; ++i) {
        pos[i*4+0] = lights_[i].pos[0];
        pos[i*4+1] = lights_[i].pos[1];
        pos[i*4+2] = lights_[i].pos[2];
        pos[i*4+3] = lights_[i].pos[3];
        col[i*3+0] = lights_[i].color[0];
        col[i*3+1] = lights_[i].color[1];
        col[i*3+2] = lights_[i].color[2];
    }
    if (posLoc   >= 0) glUniform4fv(posLoc,   kMaxLights, pos);
    if (colorLoc >= 0) glUniform3fv(colorLoc, kMaxLights, col);
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

// ---------------------------------------------------------------------------
// Geometry cache (display-list replacement)
// ---------------------------------------------------------------------------

namespace {

void setCap(GLenum cap, bool on) {
    if (on) glEnable(cap); else glDisable(cap);
}

// Reproduce sr_DepthOffset(): the wall renderer enables polygon offset for the
// depth-fighting line pass.  Mirror its exact glPolygonOffset parameters here.
void setPolygonOffset(bool on) {
    if (on) {
        glPolygonOffset(-2, -5);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glEnable(GL_POLYGON_OFFSET_LINE);
        glEnable(GL_POLYGON_OFFSET_POINT);
    } else {
        glPolygonOffset(0, 0);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_POLYGON_OFFSET_LINE);
        glDisable(GL_POLYGON_OFFSET_POINT);
    }
}

} // namespace

void ModernGLRenderer::beginRecording(rGeometryCache& cache) {
    // Flush any pending immediate-mode geometry so it isn't mixed into the cache.
    End(true);
    cache.beginRecord();
    recording_ = &cache;
}

void ModernGLRenderer::endRecording() {
    // Flush the final pending segment, then upload the cache to its static VBO.
    End(true);
    if (recording_) {
        recording_->finalize();
        recording_ = nullptr;
    }
}

void ModernGLRenderer::replayCache(const rGeometryCache& cache) {
    // Make sure no half-built immediate batch lingers.
    End(true);

    if (!cache.isValid())
        return;

    ensureShaders();
    if (!colorShader_ || !texturedShader_)
        return;

    float mv[16], proj[16];
    glGetFloatv(GL_MODELVIEW_MATRIX,  mv);
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    float nrm[9];
    computeNormalMatrix(mv, nrm);

    // Preserve the surrounding fixed-function state we are about to touch.
    GLint     prevTex   = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevPoly  = glIsEnabled(GL_POLYGON_OFFSET_FILL);

    cache.bindVAO();

    // Lighting uniforms come from the renderer's *current* state (set fresh each
    // frame before replay), so cached geometry is re-lit with up-to-date,
    // camera-relative lights — matching how fixed-function lists behaved.
    const bool savedLighting = lightingEnabled_;

    for (const CacheSegment& seg : cache.segments()) {
        const rShader& sh = (seg.texture != 0) ? *texturedShader_ : *colorShader_;
        sh.use();
        sh.setMatrix4("uModelView",  mv);
        sh.setMatrix4("uProjection", proj);
        GLint nrmLoc = sh.uniformLocation("uNormalMatrix");
        if (nrmLoc >= 0)
            glUniformMatrix3fv(nrmLoc, 1, GL_FALSE, nrm);

        lightingEnabled_ = savedLighting && seg.lit;
        applyLighting(sh);

        if (seg.texture != 0) {
            glBindTexture(GL_TEXTURE_2D, seg.texture);
            glUniform1i(sh.uniformLocation("uTexture"), 0);
        }

        setCap(GL_BLEND,      seg.blend);
        setCap(GL_DEPTH_TEST, seg.depthTest);
        setPolygonOffset(seg.polygonOffset);

        glDrawArrays(seg.prim, seg.first, seg.count);
    }

    lightingEnabled_ = savedLighting;

    glBindVertexArray(0);

    // Restore state.
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    setCap(GL_BLEND,      prevBlend == GL_TRUE);
    setCap(GL_DEPTH_TEST, prevDepth == GL_TRUE);
    setPolygonOffset(prevPoly == GL_TRUE);
    glUseProgram(0);
}

} // namespace gl

#endif // HAVE_GLEW
#endif // DEDICATED
