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
#define DONTDOIT
#include "gl_wall_instanced.h"

#ifndef DEDICATED
#ifdef HAVE_GLEW

#include <cstring>  // offsetof
#include "tConsole.h"

// ---------------------------------------------------------------------------
// GLSL shaders
//
// Template geometry:
//   Quad pass — 4 corners forming a CCW quad (two triangles via indices).
//   Line pass — 2 vertices for the top edge.
//
// Per-instance attributes (divisor=1):
//   loc 2: vec4  (p1x, p1y, p2x, p2y)
//   loc 3: vec2  (ta, te)
//   loc 4: vec4  (r, g, b, a)
//   loc 5: vec2  (h, hfrac)
//
// Template vertex attributes (divisor=0):
//   loc 0: vec2  aCorner — (side, up): side 0=p1 edge, 1=p2 edge; up 0=bottom, 1=top
// ---------------------------------------------------------------------------

static const char kWallInstVert[] = R"GLSL(
#version 330 core

layout(location = 0) in vec2 aCorner;       // (side, up): 0..1

// Per-instance (divisor=1)
layout(location = 2) in vec4 aEndpoints;    // p1x, p1y, p2x, p2y
layout(location = 3) in vec2 aTexRange;     // ta, te
layout(location = 4) in vec4 aColor;        // r, g, b, a
layout(location = 5) in vec2 aHeights;      // h, hfrac

uniform mat4 uModelView;
uniform mat4 uProjection;

out vec4 vColor;
out vec2 vTexCoord;

void main() {
    float side = aCorner.x;
    float up   = aCorner.y;

    vec2 pos2d = mix(aEndpoints.xy, aEndpoints.zw, side);
    float wallH = aHeights.x * aHeights.y;  // h * hfrac
    float z = up * wallH;                   // 0 at bottom, wallH at top

    float tx = mix(aTexRange.x, aTexRange.y, side);
    float ty = (up == 0.0) ? aHeights.y : 0.0;  // hfrac at bottom, 0 at top

    vColor    = aColor;
    vTexCoord = vec2(tx, ty);
    gl_Position = uProjection * uModelView * vec4(pos2d, z, 1.0);
}
)GLSL";

static const char kWallInstTexFrag[] = R"GLSL(
#version 330 core

in vec4 vColor;
in vec2 vTexCoord;

uniform sampler2D uTexture;

out vec4 fragColor;

void main() {
    vec4 tex = texture(uTexture, vTexCoord);
    fragColor = tex * vColor;
}
)GLSL";

static const char kWallInstColorFrag[] = R"GLSL(
#version 330 core

in vec4 vColor;
in vec2 vTexCoord;

out vec4 fragColor;

void main() {
    fragColor = vColor;
}
)GLSL";

// ---------------------------------------------------------------------------
// Template geometry
// ---------------------------------------------------------------------------

// Quad corners: (side, up) pairs — CCW winding for two triangles
// Indices: 0,1,2  0,2,3  (bottom-left → top-left → top-right → bottom-right)
static const float kQuadCorners[4][2] = {
    {0.f, 0.f},  // p1, bottom
    {0.f, 1.f},  // p1, top
    {1.f, 1.f},  // p2, top
    {1.f, 0.f},  // p2, bottom
};
static const GLushort kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

// Line corners: (side, up) for the top edge
static const float kLineCorners[2][2] = {
    {0.f, 1.f},  // p1 top
    {1.f, 1.f},  // p2 top
};

// ---------------------------------------------------------------------------
// namespace gl
// ---------------------------------------------------------------------------

namespace gl {

WallInstancedRenderer g_wallInstanced;

WallInstancedRenderer::~WallInstancedRenderer() {
    if (quadTemplateVBO_) glDeleteBuffers(1, &quadTemplateVBO_);
    if (instanceVBO_)     glDeleteBuffers(1, &instanceVBO_);
    if (lineTemplateVBO_) glDeleteBuffers(1, &lineTemplateVBO_);
    if (quadVAO_)         glDeleteVertexArrays(1, &quadVAO_);
    if (lineVAO_)         glDeleteVertexArrays(1, &lineVAO_);
}

void WallInstancedRenderer::add(const WallInstance& inst) {
    instances_.push_back(inst);
}

void WallInstancedRenderer::clear() {
    instances_.clear();
}

static void setInstanceAttribs(GLuint instanceVBO) {
    // Bind the instance VBO and describe per-instance attributes (divisor=1).
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);

    // loc 2: vec4 aEndpoints (p1x,p1y,p2x,p2y)
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(WallInstance),
        reinterpret_cast<void*>(offsetof(WallInstance, p1x)));
    glEnableVertexAttribArray(2);
    glVertexAttribDivisor(2, 1);

    // loc 3: vec2 aTexRange (ta, te)
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(WallInstance),
        reinterpret_cast<void*>(offsetof(WallInstance, ta)));
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);

    // loc 4: vec4 aColor (r, g, b, a)
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(WallInstance),
        reinterpret_cast<void*>(offsetof(WallInstance, r)));
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);

    // loc 5: vec2 aHeights (h, hfrac)
    glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(WallInstance),
        reinterpret_cast<void*>(offsetof(WallInstance, h)));
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(5, 1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void WallInstancedRenderer::ensureObjects() {
    if (quadVAO_)
        return;

    // Shared instance VBO (data uploaded per flush)
    glGenBuffers(1, &instanceVBO_);

    // ---- Quad VAO ----
    GLuint quadEBO = 0;
    glGenVertexArrays(1, &quadVAO_);
    glGenBuffers(1, &quadTemplateVBO_);
    glGenBuffers(1, &quadEBO);

    glBindVertexArray(quadVAO_);

    // Template corner geometry
    glBindBuffer(GL_ARRAY_BUFFER, quadTemplateVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadCorners), kQuadCorners, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribDivisor(0, 0); // per-vertex

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIndices), kQuadIndices, GL_STATIC_DRAW);

    setInstanceAttribs(instanceVBO_);

    glBindVertexArray(0);
    // Note: quadEBO stays bound to quadVAO_ — we intentionally do not delete it here.
    // (It will leak at process exit, acceptable for a long-lived global.)

    // ---- Line VAO ----
    glGenVertexArrays(1, &lineVAO_);
    glGenBuffers(1, &lineTemplateVBO_);

    glBindVertexArray(lineVAO_);

    glBindBuffer(GL_ARRAY_BUFFER, lineTemplateVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kLineCorners), kLineCorners, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribDivisor(0, 0);

    setInstanceAttribs(instanceVBO_);

    glBindVertexArray(0);
}

void WallInstancedRenderer::ensureShaders() {
    if (!quadShader_) {
        auto res = rShader::create(kWallInstVert, kWallInstTexFrag);
        if (res)
            quadShader_ = std::move(*res);
        else
            con << "WallInstancedRenderer: quad shader error: " << res.error() << "\n";
    }
    if (!lineShader_) {
        auto res = rShader::create(kWallInstVert, kWallInstColorFrag);
        if (res)
            lineShader_ = std::move(*res);
        else
            con << "WallInstancedRenderer: line shader error: " << res.error() << "\n";
    }
}

static void applyMVP(const rShader& sh) {
    float mv[16], proj[16];
    glGetFloatv(GL_MODELVIEW_MATRIX,  mv);
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    sh.setMatrix4("uModelView",  mv);
    sh.setMatrix4("uProjection", proj);
}

void WallInstancedRenderer::flushQuads(GLuint texture) {
    if (instances_.empty()) return;

    ensureObjects();
    ensureShaders();

    if (!quadShader_) { instances_.clear(); return; }

    // Upload instance data
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(instances_.size()) * static_cast<GLsizeiptr>(sizeof(WallInstance)),
        instances_.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    quadShader_->use();
    applyMVP(*quadShader_);

    if (texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        GLint locTex = quadShader_->uniformLocation("uTexture");
        if (locTex >= 0) glUniform1i(locTex, 0);
    }

    glBindVertexArray(quadVAO_);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr,
        static_cast<GLsizei>(instances_.size()));
    glBindVertexArray(0);

    glUseProgram(0);
    instances_.clear();
}

void WallInstancedRenderer::flushLines() {
    if (instances_.empty()) return;

    ensureObjects();
    ensureShaders();

    if (!lineShader_) { instances_.clear(); return; }

    // Lines use per-instance alpha from WallInstance::a; override color alpha.
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(instances_.size()) * static_cast<GLsizeiptr>(sizeof(WallInstance)),
        instances_.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    lineShader_->use();
    applyMVP(*lineShader_);

    glBindVertexArray(lineVAO_);
    glDrawArraysInstanced(GL_LINES, 0, 2,
        static_cast<GLsizei>(instances_.size()));
    glBindVertexArray(0);

    glUseProgram(0);
    instances_.clear();
}

} // namespace gl

#endif // HAVE_GLEW
#endif // DEDICATED
