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

#include <cmath>
#include <cstring>

// Minimal column-major 4x4 matrix used by the Vulkan backend.  Vulkan has no
// fixed-function matrix stack, so VulkanRenderer maintains its own stack of
// these (mirroring the GL matrix calls the engine still emits).  Layout matches
// OpenGL/GLSL column-major so the same shaders work unchanged.
struct vkMat4 {
    float m[16];

    static vkMat4 identity() {
        vkMat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
        return r;
    }

    // this * rhs  (column-major, OpenGL convention: result applies rhs first)
    vkMat4 operator*(const vkMat4& rhs) const {
        vkMat4 r{};
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row) {
                float sum = 0.f;
                for (int k = 0; k < 4; ++k)
                    sum += m[k * 4 + row] * rhs.m[col * 4 + k];
                r.m[col * 4 + row] = sum;
            }
        return r;
    }

    static vkMat4 translate(float x, float y, float z) {
        vkMat4 r = identity();
        r.m[12] = x; r.m[13] = y; r.m[14] = z;
        return r;
    }

    static vkMat4 scale(float x, float y, float z) {
        vkMat4 r = identity();
        r.m[0] = x; r.m[5] = y; r.m[10] = z;
        return r;
    }

    // Build from a row-major REAL[4][4] as passed to rRenderer::MultMatrix
    // (the engine stores matrices the way glMultMatrix expects: column-major in
    // a flat array, but the [4][4] form indexes [col][row]).  The existing GL
    // path feeds &mdata[0][0] straight to glMultMatrixf, i.e. column-major.
    static vkMat4 fromColMajor(const float* data) {
        vkMat4 r{};
        std::memcpy(r.m, data, sizeof(r.m));
        return r;
    }
};
