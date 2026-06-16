/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

***************************************************************************

*/

#include "eWorld.h"
#include "eGrid.h"
#include "tMemManager.h"
#include "tConsole.h"

eWorld::eWorld( eGrid * groundGrid )
    : nextId_( 0 )
    , is3D_( false )
{
    // surface 0 wraps the arena's primary grid -- the legacy ground plane
    eSurface * ground = CreateSurface( eSurfaceKind::Floor );
    ground->SetGrid( groundGrid );
    ground->SetLevels( eZLevel::Ground, eZLevel::Ground );
}

eWorld::~eWorld()
{
}

eGrid * eWorld::GroundGrid() const
{
    eSurface * ground = GroundSurface();
    return ground ? ground->Grid() : NULL;
}

eSurface * eWorld::Add( eSurface * surface )
{
    surfaces_.push_back( tJUST_CONTROLLED_PTR< eSurface >( surface ) );
    return surface;
}

eSurface * eWorld::CreateSurface( eSurfaceKind kind )
{
    eSurface * surface = tNEW( eSurface )( nextId_++, kind );
    return Add( surface );
}

eSurface * eWorld::SurfaceById( eSurfaceId id ) const
{
    for ( auto const & s : surfaces_ )
        if ( s->Id() == id )
            return s;
    return NULL;
}

eSurface * eWorld::FloorAt( eZLevel level, eCoord const & worldPoint ) const
{
    for ( auto const & s : surfaces_ )
    {
        if ( s->Kind() == eSurfaceKind::Floor &&
             s->LevelFrom() == level &&
             s->ContainsPoint( worldPoint ) )
            return s;
    }
    return NULL;
}

ePortal * eWorld::MakePortal( eSurface * a, eSurface * b,
                              eCoord const & beg, eCoord const & end )
{
    // identity local frames (local == world) in PR #1, so the seam endpoints
    // are the same on both sides; PR #2 will refine frames for rotated ramps.
    ePortal * portal = tNEW( ePortal )( a, b, beg, end, beg, end );
    portals_.push_back( tJUST_CONTROLLED_PTR< ePortal >( portal ) );
    a->AddPortal( portal );
    b->AddPortal( portal );
    return portal;
}

int eWorld::StitchRamp( eSurface * ramp,
                        eCoord const & lowBeg,  eCoord const & lowEnd,  eZLevel fromLevel,
                        eCoord const & highBeg, eCoord const & highEnd, eZLevel toLevel )
{
    int created = 0;

    eCoord lowMid  = ( lowBeg  + lowEnd  ) * 0.5;
    eCoord highMid = ( highBeg + highEnd ) * 0.5;

    eSurface * lowFloor = FloorAt( fromLevel, lowMid );
    if ( lowFloor )
    {
        MakePortal( ramp, lowFloor, lowBeg, lowEnd );
        ++created;
    }
    else
    {
        con << "Warning: ramp surface " << ramp->Id()
            << " low seam found no floor to connect to.\n";
    }

    eSurface * highFloor = FloorAt( toLevel, highMid );
    if ( highFloor )
    {
        MakePortal( ramp, highFloor, highBeg, highEnd );
        ++created;
    }
    else
    {
        con << "Warning: ramp surface " << ramp->Id()
            << " high seam found no floor to connect to.\n";
    }

    return created;
}
