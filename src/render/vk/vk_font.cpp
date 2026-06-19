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

#ifndef DEDICATED
#ifdef HAVE_VULKAN

#include "vk_font.h"
#include "vk_renderer.h"
#include "rRender.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>

namespace vk {

static FT_Library FTLib() {
    static FT_Library lib = nullptr;
    static bool inited = false;
    if ( !inited )
    {
        inited = true;
        if ( FT_Init_FreeType( &lib ) != 0 )
            lib = nullptr;
    }
    return lib;
}

static uint64_t AllocAtlasKey() {
    // Far away from the small, 0-based ids rITexture hands out and from
    // VulkanRenderer::kWhiteKey (~0), so atlas textures never collide with
    // game textures uploaded through the same map.
    static std::atomic<uint64_t> next{ uint64_t(1) << 48 };
    return next.fetch_add( 1 );
}

VkGlyphAtlas::VkGlyphAtlas( tString const & fontPath, int pixelSize )
{
    FT_Library lib = FTLib();
    if ( !lib )
        return;

    FT_Face face = nullptr;
    if ( FT_New_Face( lib, fontPath.c_str(), 0, &face ) != 0 )
        return;
    if ( FT_Set_Pixel_Sizes( face, 0, pixelSize ) != 0 )
    {
        FT_Done_Face( face );
        return;
    }

    // Pass 1: rasterize every printable-ASCII glyph and find the max cell size.
    struct Raster { std::vector<uint8_t> pixels; int w = 0, h = 0, pitch = 0,
                     bearingX = 0, bearingY = 0; float advance = 0; bool ok = false; };
    Raster rasters[95];
    int cellW = 1, cellH = 1;
    for ( unsigned i = 0; i < 95; ++i )
    {
        unsigned char c = static_cast<unsigned char>( i + 32 );
        if ( FT_Load_Char( face, c, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL ) != 0 )
            continue;
        FT_GlyphSlot slot = face->glyph;
        FT_Bitmap const & bmp = slot->bitmap;
        Raster & r = rasters[i];
        r.w = int( bmp.width );
        r.h = int( bmp.rows );
        r.bearingX = slot->bitmap_left;
        r.bearingY = slot->bitmap_top;
        r.advance  = float( slot->advance.x ) / 64.0f;
        r.ok = true;
        if ( r.w > 0 && r.h > 0 )
        {
            r.pixels.resize( size_t(r.w) * size_t(r.h) );
            int srcPitch = bmp.pitch; // positive: rows stored top-to-bottom
            for ( int y = 0; y < r.h; ++y )
            {
                unsigned char const * src = bmp.buffer + ptrdiff_t(y) * srcPitch;
                std::memcpy( &r.pixels[size_t(y) * r.w], src, size_t(r.w) );
            }
        }
        cellW = std::max( cellW, r.w + 1 );
        cellH = std::max( cellH, r.h + 1 );
    }

    int const cols = 10;
    int const rows = (95 + cols - 1) / cols;
    int const atlasW = cellW * cols;
    int const atlasH = cellH * rows;

    std::vector<uint8_t> atlas( size_t(atlasW) * size_t(atlasH), 0 );

    for ( unsigned i = 0; i < 95; ++i )
    {
        Raster const & r = rasters[i];
        GlyphInfo & g = glyphs_[i];
        if ( !r.ok )
            continue;
        int col = int(i) % cols;
        int row = int(i) / cols;
        int cellX = col * cellW;
        int cellY = row * cellH;
        if ( r.w > 0 && r.h > 0 )
        {
            for ( int y = 0; y < r.h; ++y )
            {
                std::memcpy( &atlas[size_t(cellY + y) * atlasW + cellX],
                             &r.pixels[size_t(y) * r.w], size_t(r.w) );
            }
        }
        g.u0 = float(cellX) / float(atlasW);
        g.v0 = float(cellY) / float(atlasH);
        g.u1 = float(cellX + r.w) / float(atlasW);
        g.v1 = float(cellY + r.h) / float(atlasH);
        g.width    = float(r.w);
        g.height   = float(r.h);
        g.bearingX = float(r.bearingX);
        g.bearingY = float(r.bearingY);
        g.advance  = r.advance;
        g.valid    = true;
    }

    lineHeight_ = float( face->size->metrics.height ) / 64.0f;
    FT_Done_Face( face );

    VulkanRenderer * r = GetVulkanRenderer();
    if ( !r )
        return;
    textureKey_ = AllocAtlasKey();
    ok_ = r->uploadTexture( textureKey_, atlas.data(), uint32_t(atlasW), uint32_t(atlasH),
                             TexFormat::A8, false, false, uint32_t(atlasW) );
    if ( !ok_ )
        std::cerr << "[vk_font] failed to upload glyph atlas for '" << fontPath
                   << "' size " << pixelSize << std::endl;
}

VkGlyphAtlas & VkFontCache::GetAtlas( int pixelSize )
{
    auto it = atlases_.find( pixelSize );
    if ( it != atlases_.end() )
        return it->second;
    auto inserted = atlases_.emplace( pixelSize, VkGlyphAtlas( fontPath_, pixelSize ) );
    return inserted.first->second;
}

void VkFontCache::DropAll()
{
    if ( VulkanRenderer * r = GetVulkanRenderer() )
    {
        for ( auto & entry : atlases_ )
            if ( entry.second.ok() )
                r->dropTexture( entry.second.textureKey() );
    }
    atlases_.clear();
}

void RenderTextVulkan( VkGlyphAtlas & atlas, char const * utf8, tCoord const & where,
                       float scaleX, float scaleY )
{
    VulkanRenderer * r = GetVulkanRenderer();
    if ( !r || !atlas.ok() )
        return;

    r->setCurrentTexture( atlas.textureKey() );

    PushMatrix();
    TranslateMatrix( where.x, where.y, 0. );
    ScaleMatrix( scaleX, scaleY, 1. );

    BeginQuads();
    float pen = 0;
    for ( unsigned char const * p = reinterpret_cast<unsigned char const *>( utf8 ); *p; ++p )
    {
        unsigned char c = *p;
        if ( c < 32 || c > 126 )
            continue; // non-ASCII: skip (FTGL path handles full UTF-8; out of scope here)
        GlyphInfo const & g = atlas.Glyph( c );
        if ( g.valid && g.width > 0 && g.height > 0 )
        {
            float l = pen + g.bearingX;
            float t = g.bearingY;
            float rr = l + g.width;
            float b = t - g.height;

            TexCoord( g.u0, g.v1 ); Vertex( l,  b );
            TexCoord( g.u1, g.v1 ); Vertex( rr, b );
            TexCoord( g.u1, g.v0 ); Vertex( rr, t );
            TexCoord( g.u0, g.v0 ); Vertex( l,  t );
        }
        pen += g.valid ? g.advance : 0;
    }
    RenderEnd( true );

    PopMatrix();
}

} // namespace vk

#endif // HAVE_VULKAN
#endif // DEDICATED
