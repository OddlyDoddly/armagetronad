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

#ifndef ArmageTron_SURFACE_H
#define ArmageTron_SURFACE_H

#include "defs.h"
#include "eCoord.h"
#include "tSafePTR.h"
#include <vector>

class eGrid;
class ePortal;

//! identifies a drivable surface within an eWorld; 0 is the legacy ground plane
typedef unsigned short eSurfaceId;

//! the three discrete height bands a surface can live on
enum class eZLevel
{
    Basement = -1,
    Ground   =  0,
    Air       =  1
};

//! what kind of primitive produced a surface
enum class eSurfaceKind
{
    Floor,   //!< flat drivable panel
    Ramp,    //!< fixed-angle slope connecting two levels
    Tunnel   //!< drivable surface running under another (overhang)
};

//! how a surface paints its floor (rendering hint, consumed by the renderer)
enum class eSurfaceMaterial
{
    Grid,    //!< classic translucent grid
    Light,   //!< solid lit tile
    Glass    //!< see-through floor with a look-up effect
};

//! A single planar chart of the surface graph.
//!
//! Each eSurface wraps its OWN planar eGrid (composition, not inheritance):
//! the engine's eGrid is a planar DCEL and cannot represent two drivable
//! surfaces that overlap in (x,y), so overhangs require one grid per surface.
//! Within a surface everything is plain 2D and all existing eGrid / movement /
//! rubber / winding code applies unchanged. The third dimension is DERIVED from
//! a per-surface z-mapping (constant for flats, a fixed linear rise for ramps)
//! and is used for rendering only -- it is never simulated.
class eSurface : public tReferencable< eSurface >
{
    friend class tReferencable< eSurface >;

public:
    eSurface( eSurfaceId id, eSurfaceKind kind );
    eGrid * Grid() const { return grid_; }
    void    SetGrid( eGrid * grid );

    eSurfaceId   Id()       const { return id_; }
    eSurfaceKind Kind()     const { return kind_; }
    eZLevel      LevelFrom() const { return zFrom_; }
    eZLevel      LevelTo()   const { return zTo_; }
    eSurfaceMaterial Material() const { return material_; }

    void SetLevels( eZLevel from, eZLevel to ) { zFrom_ = from; zTo_ = to; }
    void SetMaterial( eSurfaceMaterial m ) { material_ = m; }

    //! local 2D frame: world(local) = origin_ + ex_*local.x + ey_*local.y
    void SetFrame( eCoord const & origin, eCoord const & ex, eCoord const & ey );

    //! z-mapping in the surface's local frame: z = z0_ + grad_ . local
    void SetZMap( REAL z0, eCoord const & grad ) { z0_ = z0; grad_ = grad; }

    //! derived render-height at a local-frame position
    REAL ZAt( eCoord const & local ) const { return z0_ + grad_.x*local.x + grad_.y*local.y; }

    //! base height of the surface (z at the local origin)
    REAL BaseZ() const { return z0_; }

    void AddPortal( ePortal * portal );
    std::vector< ePortal * > const & Portals() const { return portals_; }

    //! world-space boundary polygon of this surface (used for adjacency tests)
    void SetFootprint( std::vector< eCoord > const & footprint ) { footprint_ = footprint; }
    std::vector< eCoord > const & Footprint() const { return footprint_; }

    //! true if the world-space point lies inside this surface's footprint
    bool ContainsPoint( eCoord const & p ) const;

protected:
    ~eSurface();

private:
    eSurfaceId   id_;
    eSurfaceKind kind_;
    eZLevel      zFrom_, zTo_;
    eSurfaceMaterial material_;

    tJUST_CONTROLLED_PTR< eGrid > grid_;

    eCoord origin_, ex_, ey_;   //!< local -> world frame
    REAL   z0_;                 //!< height at local origin
    eCoord grad_;               //!< height gradient in local frame (0 for flats)

    std::vector< eCoord > footprint_;  //!< world-space boundary polygon
    std::vector< ePortal * > portals_; //!< seams to neighbouring surfaces (not owned)
};

//! A seam joining two surfaces. Crossing it (PR #2) hands a game object from
//! one surface's grid to the other and remaps its position/direction between
//! the two local frames. In PR #1 portals are recorded but inert.
class ePortal : public tReferencable< ePortal >
{
    friend class tReferencable< ePortal >;

public:
    ePortal( eSurface * a, eSurface * b,
             eCoord const & aBeg, eCoord const & aEnd,
             eCoord const & bBeg, eCoord const & bEnd );

    eSurface * A() const { return a_; }
    eSurface * B() const { return b_; }

    //! the other surface joined by this portal
    eSurface * Other( eSurface const * from ) const { return from == a_ ? b_ : a_; }

    //! map a point on the seam from one surface's local frame to the other's
    eCoord MapPoint( eSurface const * from, eCoord const & p ) const;
    //! map a direction from one surface's local frame to the other's
    eCoord MapDir( eSurface const * from, eCoord const & d ) const;

protected:
    ~ePortal();

private:
    tJUST_CONTROLLED_PTR< eSurface > a_, b_;
    eCoord aBeg_, aEnd_;   //!< seam endpoints in a_'s local frame
    eCoord bBeg_, bEnd_;   //!< the same seam in b_'s local frame
};

#endif // ArmageTron_SURFACE_H
