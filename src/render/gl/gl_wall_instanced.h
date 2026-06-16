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
#include "rGL.h"

#ifndef DEDICATED
#ifdef HAVE_GLEW

#include "rShader.h"
#include <vector>
#include <optional>

// Per-instance data for a bulk wall segment (RenderNormal).
// Matches the layout expected by wall_instanced.vert attribute locations 2-6.
struct WallInstance {
    float p1x, p1y;   // start point (world XY)
    float p2x, p2y;   // end point
    float ta,  te;    // texcoord U at p1 / p2
    float r, g, b, a; // color (a applied to lines only; quads use a=1)
    float h, hfrac;   // wall height scale and fractional height (death fade)
    float zbase = 0;  // base height of the wall's surface (surface-graph; 0 == ground)
};

namespace gl {

// Collects WallInstance records during a wall render pass and issues one
// glDrawArraysInstanced call per primitive type (lines / quads) at flush().
// Gate all calls behind sg_modernRenderer.
class WallInstancedRenderer {
public:
    WallInstancedRenderer()  = default;
    ~WallInstancedRenderer();

    WallInstancedRenderer(const WallInstancedRenderer&)            = delete;
    WallInstancedRenderer& operator=(const WallInstancedRenderer&) = delete;

    // Accumulate one wall segment for the next flush().
    void add(const WallInstance& inst);

    // Upload instances and issue instanced draw calls; clears the list.
    // Must be called with the appropriate texture bound (or unbound).
    void flushQuads(GLuint texture);
    void flushLines();

    // Drop accumulated data without drawing (e.g. on context loss).
    void clear();

private:
    void ensureObjects();
    void ensureShaders();

    std::vector<WallInstance> instances_;

    // Template geometry VAOs: one for the quad pass, one for the line pass.
    GLuint quadVAO_ = 0, quadTemplateVBO_ = 0;
    GLuint lineVAO_ = 0, lineTemplateVBO_ = 0;
    // Shared per-instance VBO (re-uploaded on every flush).
    GLuint instanceVBO_ = 0;

    std::optional<rShader> quadShader_;
    std::optional<rShader> lineShader_;
};

// Global instance — one per process.
extern WallInstancedRenderer g_wallInstanced;

} // namespace gl

#endif // HAVE_GLEW
#endif // DEDICATED
