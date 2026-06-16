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

#ifndef ArmageTron_WORLD_H
#define ArmageTron_WORLD_H

#include "defs.h"
#include "eCoord.h"
#include "tSafePTR.h"
#include "eSurface.h"
#include <vector>

class eGrid;

//! The set of drivable surfaces making up one arena (the "surface graph").
//!
//! eWorld is a thin registry that supplements the legacy single-grid model:
//! surface 0 always wraps the arena's primary eGrid, so all existing
//! eGrid*-based code keeps working by calling GroundGrid(). When 3D is active a
//! map may register additional floor/ramp surfaces (each with its own grid),
//! stitched together by portals. When 3D is inactive the world holds exactly
//! one surface and behaviour is identical to the classic 2D game.
class eWorld : public tReferencable< eWorld >
{
    friend class tReferencable< eWorld >;

public:
    //! create a world whose ground surface (id 0) wraps the given grid
    explicit eWorld( eGrid * groundGrid );

    eSurface * GroundSurface() const { return surfaces_.empty() ? NULL : surfaces_.front(); }
    eGrid *    GroundGrid()    const;

    //! register a new surface; the world takes a reference. Returns it back.
    eSurface * Add( eSurface * surface );

    //! create and register a fresh surface of the given kind with the next id
    eSurface * CreateSurface( eSurfaceKind kind );

    eSurface * SurfaceById( eSurfaceId id ) const;
    int        SurfaceCount() const { return static_cast<int>( surfaces_.size() ); }
    eSurface * SurfaceByIndex( int i ) const { return surfaces_[i]; }

    //! find a Floor surface at the given level whose footprint contains p
    eSurface * FloorAt( eZLevel level, eCoord const & worldPoint ) const;

    //! create and register a portal joining two surfaces along a seam, and
    //! record it on both surfaces. Returns the portal (owned by the world).
    ePortal * Connect( eSurface * a, eSurface * b,
                       eCoord const & beg, eCoord const & end );

    //! link a ramp to the floors it meets at its low/high seams, creating
    //! portals. Returns the number of portals created (0..2). Warns when a
    //! seam finds no matching floor (a surface would otherwise be an island).
    int StitchRamp( eSurface * ramp,
                    eCoord const & lowBeg,  eCoord const & lowEnd,  eZLevel fromLevel,
                    eCoord const & highBeg, eCoord const & highEnd, eZLevel toLevel );

    bool Is3D() const { return is3D_; }
    void SetIs3D( bool v ) { is3D_ = v; }

    int PortalCount() const { return static_cast<int>( portals_.size() ); }

protected:
    ~eWorld();

private:
    eSurfaceId nextId_;
    bool       is3D_;
    std::vector< tJUST_CONTROLLED_PTR< eSurface > > surfaces_;
    std::vector< tJUST_CONTROLLED_PTR< ePortal  > > portals_;
};

#endif // ArmageTron_WORLD_H
