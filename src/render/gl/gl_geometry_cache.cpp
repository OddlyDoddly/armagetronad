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

// DONTDOIT suppresses the glBegin/glEnd/glMatrixMode error-macros so the GL
// compatibility entry points stay usable from this translation unit.
#define DONTDOIT

#include "gl_geometry_cache.h"

#ifndef DEDICATED
#ifdef HAVE_GLEW

#include <cstring>  // offsetof

namespace gl {

rGeometryCache::~rGeometryCache() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

void rGeometryCache::ensureObjects() {
    if (vao_ && vbo_)
        return;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    // Same attribute layout as ModernGLRenderer's streaming VAO:
    //   loc 0: xyzw, loc 1: rgba, loc 2: stpq
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex),
        reinterpret_cast<void*>(offsetof(BatchVertex, x)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex),
        reinterpret_cast<void*>(offsetof(BatchVertex, r)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex),
        reinterpret_cast<void*>(offsetof(BatchVertex, s)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(BatchVertex),
        reinterpret_cast<void*>(offsetof(BatchVertex, nx)));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void rGeometryCache::beginRecord() {
    pending_.clear();
    segments_.clear();
    recording_ = true;
    valid_     = false;
}

void rGeometryCache::append(GLenum prim, const BatchVertex* data, std::size_t count,
                            GLuint texture, bool blend, bool depthTest, bool polygonOffset,
                            bool lit) {
    if (!recording_ || count == 0)
        return;

    CacheSegment seg;
    seg.prim          = prim;
    seg.first         = static_cast<GLint>(pending_.size());
    seg.count         = static_cast<GLsizei>(count);
    seg.texture       = texture;
    seg.blend         = blend;
    seg.depthTest     = depthTest;
    seg.polygonOffset = polygonOffset;
    seg.lit           = lit;

    pending_.insert(pending_.end(), data, data + count);
    segments_.push_back(seg);
}

void rGeometryCache::finalize() {
    recording_ = false;

    if (pending_.empty() || segments_.empty()) {
        valid_ = false;
        return;
    }

    ensureObjects();

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(pending_.size()) * static_cast<GLsizeiptr>(sizeof(BatchVertex)),
        pending_.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // The CPU copy is no longer needed once it lives in the static VBO.
    pending_.clear();
    pending_.shrink_to_fit();

    valid_ = true;
}

void rGeometryCache::reset() {
    pending_.clear();
    segments_.clear();
    recording_ = false;
    valid_     = false;
}

void rGeometryCache::bindVAO() const {
    glBindVertexArray(vao_);
}

} // namespace gl

#endif // HAVE_GLEW
#endif // DEDICATED
