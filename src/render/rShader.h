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

#ifndef RETROCYCLES_RSHADER_H
#define RETROCYCLES_RSHADER_H

#include "aa_config.h"
// rGL.h includes rGLEW.h which defines HAVE_GLEW — must come before the guard below
#include "rGL.h"

#ifndef DEDICATED
#ifdef HAVE_GLEW

#include <expected>
#include <string>

// RAII wrapper for a linked GLSL program. Compile vert+frag, use, set uniforms.
class rShader {
public:
    static std::expected<rShader, std::string> create(
        const char* vertSrc, const char* fragSrc);

    ~rShader();
    rShader(rShader&&) noexcept;
    rShader& operator=(rShader&&) noexcept;
    rShader(const rShader&) = delete;
    rShader& operator=(const rShader&) = delete;

    void use() const;
    GLint uniformLocation(const char* name) const;

    void setFloat(const char* name, float v) const;
    void setVec2(const char* name, float x, float y) const;
    void setVec3(const char* name, float x, float y, float z) const;
    void setMatrix4(const char* name, const float* m, bool transpose = false) const;

private:
    explicit rShader(GLuint prog) noexcept : program_(prog) {}
    GLuint program_ = 0;
};

#endif // HAVE_GLEW
#endif // DEDICATED
#endif // RETROCYCLES_RSHADER_H
