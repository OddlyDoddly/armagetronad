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

#include "aa_config.h"

#ifndef DEDICATED
#ifdef HAVE_VULKAN

#include "tString.h"
#include "tCoord.h"
#include <map>
#include <vector>
#include <cstdint>

// Vulkan has no analog of FTGL's GL-backed texture fonts (FTGL draws straight
// to GL internally, see rFont.cpp's rFontContainer::Render()).  Rather than
// depend on FTGL's private GL state, this module loads the same .ttf file a
// second time with FreeType2 directly (already an independent linked
// dependency, see configure.ac) and rasterizes a fixed printable-ASCII glyph
// atlas per font size, uploaded once through vk::VulkanRenderer::uploadTexture
// (the same texture path general game textures use).  FTGL is still used
// elsewhere for layout (Advance/BBox/LineHeight) -- this module only replaces
// the pixel-producing step.
namespace vk {

struct GlyphInfo {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // atlas UVs
    float width = 0, height = 0;          // bitmap size, pixels
    float bearingX = 0, bearingY = 0;     // pixels, top-left origin convention
    float advance = 0;                    // pixels
    bool  valid = false;
};

// One rasterized glyph atlas for a single (font file, pixel size) pair.
class VkGlyphAtlas {
public:
    VkGlyphAtlas(tString const & fontPath, int pixelSize);

    bool ok() const { return ok_; }
    float lineHeight() const { return lineHeight_; }
    uint64_t textureKey() const { return textureKey_; }

    // Printable ASCII only (32-126); anything else maps to GlyphInfo::valid==false.
    GlyphInfo const & Glyph(unsigned char c) const { return glyphs_[GlyphIndex(c)]; }

private:
    static unsigned GlyphIndex(unsigned char c) {
        return (c >= 32 && c <= 126) ? unsigned(c - 32) : 0;
    }

    bool     ok_ = false;
    float    lineHeight_ = 0;
    uint64_t textureKey_ = 0;
    GlyphInfo glyphs_[95];
};

// Cache of atlases keyed by pixel size, mirroring rFontContainer's own
// std::map<int,FTFont*> size bucketing (rFont.cpp).  One cache per font file.
class VkFontCache {
public:
    explicit VkFontCache(tString const & fontPath) : fontPath_(fontPath) {}

    VkGlyphAtlas & GetAtlas(int pixelSize);

    // Retire every atlas's GPU texture (called when the font is reloaded).
    void DropAll();

private:
    tString fontPath_;
    std::map<int, VkGlyphAtlas> atlases_;
};

// Emits one textured quad per glyph of an UTF-8/Latin1 byte string (non-ASCII
// bytes fall back to the atlas's "missing glyph" -- currently just skipped)
// using the engine's existing immediate-mode rRenderer interface
// (BeginQuads/TexCoord/Vertex/End), so it works through the same matrix stack
// and vertex-color state the GL path already relies on.
// 'where' and 'scale' are in the same normalized screen units rFontContainer
// ::Render() converts into via glTranslatef/glScalef on the GL path.
void RenderTextVulkan(VkGlyphAtlas & atlas, char const * utf8, tCoord const & where,
                      float scaleX, float scaleY);

} // namespace vk

#endif // HAVE_VULKAN
#endif // DEDICATED
