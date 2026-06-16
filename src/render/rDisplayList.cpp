/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2008  Manuel Moos (manuel@moosnet.de) and the AA Development Team

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
  
***************************************************************************

*/

#include "rDisplayList.h"

#include "rScreen.h"

#ifdef DEBUG
#ifndef DEDICATED
#define LIST_STATS
#endif
#endif

#include "rRender.h"

#ifndef DEDICATED
#ifdef HAVE_GLEW
#include "gl/gl_backend.h"
#include "gl/gl_geometry_cache.h"

// When the modern renderer is active, the global renderer is a ModernGLRenderer.
static gl::ModernGLRenderer * sr_modernRenderer()
{
    return sg_modernRenderer ? static_cast< gl::ModernGLRenderer * >( renderer ) : nullptr;
}
#endif
#endif

#ifdef LIST_STATS
class rListCounter
{
public:
    enum Type
    {
        Use,    // display list used
        Create, // created
        Not,    // not used
        COUNT
    };

    rListCounter(){ count_[Use] = count_[Create] = count_[Not] = 0; }
    void Count( Type type )
    {
        count_[type]++;
    }
    ~rListCounter()
    {
        std::cout << "Display Lists Created: " << count_[Create] << ", used: " << count_[Use]
                  << " and passed: " << count_[Not] << ".\n";
    }
private:
    unsigned int count_[COUNT];
};

static rListCounter sr_counter;
#endif

#ifndef DEDICATED
static rDisplayList * se_displayListAnchor = NULL;
#endif

rDisplayList::rDisplayList()
#ifndef DEDICATED
    : tListItem< rDisplayList >( se_displayListAnchor )
    , list_( 0 )
    , inhibit_( 0 )
    , filling_( false )
#endif
{
}

rDisplayList::~rDisplayList()
{
#ifndef DEDICATED
    tASSERT( !filling_ );

    Clear();

    tASSERT( !list_ );
#endif
}

static rDisplayListFiller * sr_currentFiller = NULL;

//! check whether a displaylist is currently being recorded.
bool rDisplayList::IsRecording()
{
    return sr_currentFiller;
}

// calls the display list, returns true if there was a list to call
bool rDisplayList::OnCall()
{
#ifndef DEDICATED
    tASSERT( !filling_ );

    // Modern renderer: replay the VAO/VBO geometry cache instead of a GL list.
    if ( sg_modernRenderer )
    {
#ifdef HAVE_GLEW
        // abort previous glBegin block
        RenderEnd();

        // no playback while another list is recorded; gives us a chance to
        // agglomerate primitives, matching the GL display-list behaviour.
        if ( IsRecording() )
            return false;

        if ( inhibit_ > 0 )
        {
            if ( cache_ && cache_->isValid() )
                Clear( 0 );
            --inhibit_;
            return false;
        }

        gl::ModernGLRenderer * mr = sr_modernRenderer();
        if ( mr && cache_ && cache_->isValid() )
        {
#ifdef LIST_STATS
            sr_counter.Count( rListCounter::Use );
#endif
            mr->replayCache( *cache_ );
            return true;
        }
#endif
        return false;
    }

    // abort previous glBegin block
    RenderEnd();

    // no playback while another list is recorded; this
    // gives us a chance to agglomerate primitives.
    if ( IsRecording() )
    {
        return false;
    }

    if ( inhibit_ > 0 )
    {
        if ( list_ )
            Clear( 0 );
        --inhibit_;
        return false;
    }

    if ( list_ )
    {
#ifdef LIST_STATS
        sr_counter.Count( rListCounter::Use );
#endif
        glCallList( list_ );
        return true;
    }
#endif

    return false;
}

//! clears the display list and don't regenerate it for the next few calls
void rDisplayList::Clear( int inhibitGeneration )
{
#ifndef DEDICATED
#ifdef HAVE_GLEW
    // Modern renderer: invalidate the geometry cache instead of a GL list.
    if ( sg_modernRenderer )
    {
        if ( !filling_ && cache_ )
        {
            cache_->reset();
        }
        else if ( cache_ && cache_->isValid() && inhibitGeneration < 1 )
        {
            // clear it later
            inhibitGeneration = 1;
        }

        if ( inhibit_ < inhibitGeneration )
        {
            inhibit_ = inhibitGeneration;
        }
        return;
    }
#endif

    // clear the list
    if ( !filling_ && list_ )
    {
        // abort previous glBegin block
        if ( renderer )
        {
            RenderEnd();
        }

        glDeleteLists( list_, 1 );
        list_ = 0;
    }
    else
    {
        // clear it later
        if ( list_ && inhibitGeneration < 1 )
        {
            inhibitGeneration = 1;
        }
    }

    // memorize inhibit counter
    if ( inhibit_ < inhibitGeneration )
    {
        inhibit_ = inhibitGeneration;
    }
#endif
}

// clears all display lists
void rDisplayList::ClearAll()
{
#ifndef DEDICATED
    Cancel();
    tASSERT(!IsRecording());

    rDisplayList *run = se_displayListAnchor;
    while (run)
    {
        tASSERT( !run->filling_ );
        run->Clear();
        tASSERT( !run->list_ );
        run = run->Next();
    }
#endif
}

// cancels recording of current display list
void rDisplayList::Cancel()
{
#ifndef DEDICATED
    if ( sr_currentFiller )
    {
        sr_currentFiller->list_.Clear(0);
        sr_currentFiller->Stop();
    }
#endif
}

rDisplayListAlphaSensitive::rDisplayListAlphaSensitive()
: lastAlpha_( sr_alphaBlend )
{}

bool rDisplayListAlphaSensitive::OnCall()
{
    // check whether crucial settings changed
    if ( sr_alphaBlend != lastAlpha_ )
    {
        lastAlpha_ = sr_alphaBlend;
        return false;
    }

    return rDisplayList::OnCall();
}

//! constructor, automatically starting to fill teh list
rDisplayListFiller::rDisplayListFiller( rDisplayList & list, bool respectBlacklist )
#ifndef DEDICATED
    : list_( list )
#endif
{
    Start( respectBlacklist );
}

// starts filling the display list
void rDisplayListFiller::Start( bool respectBlacklist )
{
#ifndef DEDICATED
#ifdef HAVE_GLEW
    // Modern renderer: record the renderer's BatchVertex stream into a cache.
    if ( sg_modernRenderer )
    {
        gl::ModernGLRenderer * mr = sr_modernRenderer();

        bool useList = sr_useDisplayLists != rDisplayList_Off && list_.inhibit_ == 0
                       && !sr_currentFiller && mr;
#ifndef DEBUG_X
        if ( sr_blacklistDisplayLists && respectBlacklist )
        {
            useList = false;
        }
#endif

        if ( useList )
        {
#ifdef LIST_STATS
            sr_counter.Count( rListCounter::Create );
#endif
            if ( !list_.cache_ )
            {
                list_.cache_ = std::make_unique< gl::rGeometryCache >();
            }
            mr->beginRecording( *list_.cache_ );
            list_.filling_   = true;
            sr_currentFiller = this;
        }
        else if ( list_.inhibit_ > 0 )
        {
            --list_.inhibit_;
        }
        return;
    }
#endif

    bool useList = sr_useDisplayLists != rDisplayList_Off && list_.inhibit_ == 0 && !sr_currentFiller;

    // don't ever use display lists if they are blacklisted
#ifndef DEBUG_X
    if ( sr_blacklistDisplayLists && respectBlacklist )
    {
        useList = false;
    }
#endif

    if ( useList )
    {
#ifdef LIST_STATS
        sr_counter.Count( rListCounter::Create );
#endif
        if ( !list_.list_ )
        {
            list_.list_=glGenLists(1);
            tASSERT( list_.list_ );
        }
        glNewList(list_.list_, sr_useDisplayLists == rDisplayList_CAC ? GL_COMPILE : GL_COMPILE_AND_EXECUTE );
        list_.filling_ = true;
        sr_currentFiller = this;
    }
    else if ( list_.inhibit_ > 0 )
    {
        --list_.inhibit_;
    }

#ifdef LIST_STATS
    if ( !useList && !sr_currentFiller )
    {
        sr_counter.Count( rListCounter::Not );
    }
#endif
#endif
}

rDisplayListFiller::~rDisplayListFiller()
{
    Stop();
}

//! stops filling the display list (done automatically on destruction)
void rDisplayListFiller::Stop()
{
#ifndef DEDICATED
#ifdef HAVE_GLEW
    // Modern renderer: finalize the geometry cache (uploads to a static VBO).
    if ( sg_modernRenderer )
    {
        if ( list_.filling_ )
        {
            tASSERT( sr_currentFiller == this );

            gl::ModernGLRenderer * mr = sr_modernRenderer();
            if ( mr )
            {
                mr->endRecording();
            }

            sr_currentFiller = 0;
            list_.filling_   = false;
        }
        return;
    }
#endif

    if ( list_.filling_ )
    {
        tASSERT( list_.list_ );
        tASSERT( sr_currentFiller == this );

        // close previous glBegin block
        RenderEnd();

        sr_currentFiller = 0;
        list_.filling_ = false;
        glEndList();

        if ( sr_useDisplayLists == rDisplayList_CAC )
        {
            // call the list, making sure it really gets executed
            int inhibit = list_.inhibit_;
            list_.inhibit_ = 0;
            list_.Call();
            list_.inhibit_ = inhibit;
        }
    }

    // to debug, clearing every display list may be useful:
    // list_.Clear(0);
#endif
}

static rCallbackBeforeScreenModeChange sr_unload( &rDisplayList::ClearAll );
