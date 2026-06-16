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

#include "gl_backend.h"   // BatchVertex

#include <vector>

namespace gl {

// A single drawable run within a cached geometry list.  One segment is
// produced for every flush() of the modern renderer that happens while a
// list is being recorded, so the GL render-state in effect at record time
// (texture binding, blending, depth test, depth offset) can be faithfully
// reproduced on replay.
struct CacheSegment {
    GLenum  prim          = GL_TRIANGLES; //!< post-tessellation primitive
    GLint   first         = 0;            //!< first vertex in the static VBO
    GLsizei count         = 0;            //!< vertex count
    GLuint  texture       = 0;            //!< bound GL_TEXTURE_2D (0 = none)
    bool    blend         = false;        //!< GL_BLEND enabled
    bool    depthTest     = true;         //!< GL_DEPTH_TEST enabled
    bool    polygonOffset = false;        //!< GL_POLYGON_OFFSET_FILL enabled
    bool    lit           = false;        //!< GLSL lighting enabled at record time
};

// VAO/VBO-backed replacement for a GL display list.  The modern renderer
// records its post-tessellation BatchVertex stream here while a list fills,
// uploads it to a single static VBO on finalize(), and replays it on demand.
//
// RAII: owns one VAO + one VBO, released in the destructor.
class rGeometryCache {
public:
    rGeometryCache() = default;
    ~rGeometryCache();

    rGeometryCache(const rGeometryCache&)            = delete;
    rGeometryCache& operator=(const rGeometryCache&) = delete;

    //! Begin a fresh recording, discarding any previously cached geometry.
    void beginRecord();

    //! Append one drawable segment (vertices already tessellated to a
    //! GL 3.3-core primitive) together with the render-state to reproduce.
    void append(GLenum prim, const BatchVertex* data, std::size_t count,
                GLuint texture, bool blend, bool depthTest, bool polygonOffset,
                bool lit);

    //! Upload the accumulated vertices to the static VBO and build the VAO.
    void finalize();

    //! Discard cached geometry (the GL objects are kept for reuse).
    void reset();

    //! Whether there is replayable geometry.
    bool isValid() const { return valid_ && !segments_.empty(); }

    void bindVAO() const;
    const std::vector<CacheSegment>& segments() const { return segments_; }

private:
    void ensureObjects();

    std::vector<BatchVertex>  pending_;   //!< accumulated during recording
    std::vector<CacheSegment> segments_;  //!< draw runs

    GLuint vao_       = 0;
    GLuint vbo_       = 0;
    bool   recording_ = false;
    bool   valid_     = false;
};

} // namespace gl

#endif // HAVE_GLEW
#endif // DEDICATED
