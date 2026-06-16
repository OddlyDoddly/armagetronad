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
// rShader.h pulls in rGL.h → rGLEW.h which defines HAVE_GLEW
#include "rShader.h"
#include "tConsole.h"

#ifndef DEDICATED
#ifdef HAVE_GLEW

namespace {

std::string shaderLog(GLuint s) {
    GLint len = 0;
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1) return {};
    std::string log(static_cast<std::size_t>(len), '\0');
    glGetShaderInfoLog(s, len, nullptr, log.data());
    return log;
}

std::string programLog(GLuint p) {
    GLint len = 0;
    glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1) return {};
    std::string log(static_cast<std::size_t>(len), '\0');
    glGetProgramInfoLog(p, len, nullptr, log.data());
    return log;
}

std::expected<GLuint, std::string> compileStage(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        auto log = shaderLog(s);
        glDeleteShader(s);
        return std::unexpected(std::move(log));
    }
    return s;
}

} // namespace

std::expected<rShader, std::string> rShader::create(
    const char* vertSrc, const char* fragSrc)
{
    auto vert = compileStage(GL_VERTEX_SHADER, vertSrc);
    if (!vert) return std::unexpected(std::move(vert.error()));

    auto frag = compileStage(GL_FRAGMENT_SHADER, fragSrc);
    if (!frag) {
        glDeleteShader(*vert);
        return std::unexpected(std::move(frag.error()));
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, *vert);
    glAttachShader(prog, *frag);
    glLinkProgram(prog);
    glDeleteShader(*vert);
    glDeleteShader(*frag);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        auto log = programLog(prog);
        glDeleteProgram(prog);
        return std::unexpected(std::move(log));
    }
    return rShader(prog);
}

rShader::~rShader() {
    if (program_) glDeleteProgram(program_);
}

rShader::rShader(rShader&& o) noexcept : program_(o.program_) {
    o.program_ = 0;
}

rShader& rShader::operator=(rShader&& o) noexcept {
    if (this != &o) {
        if (program_) glDeleteProgram(program_);
        program_ = o.program_;
        o.program_ = 0;
    }
    return *this;
}

void rShader::use() const {
    glUseProgram(program_);
}

GLint rShader::uniformLocation(const char* name) const {
    return glGetUniformLocation(program_, name);
}

void rShader::setFloat(const char* name, float v) const {
    glUniform1f(uniformLocation(name), v);
}

void rShader::setVec2(const char* name, float x, float y) const {
    glUniform2f(uniformLocation(name), x, y);
}

void rShader::setVec3(const char* name, float x, float y, float z) const {
    glUniform3f(uniformLocation(name), x, y, z);
}

void rShader::setMatrix4(const char* name, const float* m, bool transpose) const {
    glUniformMatrix4fv(
        uniformLocation(name), 1,
        transpose ? GL_TRUE : GL_FALSE,
        m);
}

#endif // HAVE_GLEW
#endif // DEDICATED
