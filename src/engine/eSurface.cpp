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

#include "eSurface.h"
#include "eGrid.h"

// ---------------------------------------------------------------------------
// eSurface
// ---------------------------------------------------------------------------

eSurface::eSurface( eSurfaceId id, eSurfaceKind kind )
    : id_( id )
    , kind_( kind )
    , zFrom_( eZLevel::Ground )
    , zTo_( eZLevel::Ground )
    , material_( eSurfaceMaterial::Grid )
    , grid_( NULL )
    , origin_( 0, 0 )
    , ex_( 1, 0 )
    , ey_( 0, 1 )
    , z0_( 0 )
    , grad_( 0, 0 )
{
}

eSurface::~eSurface()
{
}

void eSurface::SetGrid( eGrid * grid )
{
    grid_ = grid;
}

void eSurface::SetFrame( eCoord const & origin, eCoord const & ex, eCoord const & ey )
{
    origin_ = origin;
    ex_ = ex;
    ey_ = ey;
}

void eSurface::AddPortal( ePortal * portal )
{
    portals_.push_back( portal );
}

bool eSurface::ContainsPoint( eCoord const & p ) const
{
    // standard ray-casting point-in-polygon test against the world footprint
    size_t n = footprint_.size();
    if ( n < 3 )
        return false;

    bool inside = false;
    for ( size_t i = 0, j = n - 1; i < n; j = i++ )
    {
        eCoord const & a = footprint_[i];
        eCoord const & b = footprint_[j];
        if ( ( ( a.y > p.y ) != ( b.y > p.y ) ) &&
             ( p.x < ( b.x - a.x ) * ( p.y - a.y ) / ( b.y - a.y ) + a.x ) )
            inside = !inside;
    }
    return inside;
}

// ---------------------------------------------------------------------------
// ePortal
// ---------------------------------------------------------------------------

ePortal::ePortal( eSurface * a, eSurface * b,
                  eCoord const & aBeg, eCoord const & aEnd,
                  eCoord const & bBeg, eCoord const & bEnd )
    : a_( a )
    , b_( b )
    , aBeg_( aBeg )
    , aEnd_( aEnd )
    , bBeg_( bBeg )
    , bEnd_( bEnd )
{
}

ePortal::~ePortal()
{
}

eCoord ePortal::MapPoint( eSurface const * from, eCoord const & p ) const
{
    // Express p as a parameter t along the seam on the source side, then place
    // it at the same parameter on the destination side. Endpoints are stored in
    // each surface's own local frame, so this remaps across frames as well.
    eCoord const & srcBeg = ( from == a_ ) ? aBeg_ : bBeg_;
    eCoord const & srcEnd = ( from == a_ ) ? aEnd_ : bEnd_;
    eCoord const & dstBeg = ( from == a_ ) ? bBeg_ : aBeg_;
    eCoord const & dstEnd = ( from == a_ ) ? bEnd_ : aEnd_;

    eCoord seam = srcEnd - srcBeg;
    REAL len2 = seam.NormSquared();
    REAL t = ( len2 > EPS ) ? ( tCoord::F( p - srcBeg, seam ) / len2 ) : 0;

    return dstBeg + ( dstEnd - dstBeg ) * t;
}

eCoord ePortal::MapDir( eSurface const * from, eCoord const & d ) const
{
    // Rotate the direction by the angle between the two seams. Using complex
    // multiplication: dstSeamDir * conj-normalised(srcSeamDir).
    eCoord const & srcBeg = ( from == a_ ) ? aBeg_ : bBeg_;
    eCoord const & srcEnd = ( from == a_ ) ? aEnd_ : bEnd_;
    eCoord const & dstBeg = ( from == a_ ) ? bBeg_ : aBeg_;
    eCoord const & dstEnd = ( from == a_ ) ? bEnd_ : aEnd_;

    eCoord srcSeam = srcEnd - srcBeg;
    eCoord dstSeam = dstEnd - dstBeg;
    REAL srcNorm = srcSeam.Norm();
    REAL dstNorm = dstSeam.Norm();
    if ( srcNorm <= EPS || dstNorm <= EPS )
        return d;

    srcSeam = srcSeam * ( 1 / srcNorm );
    dstSeam = dstSeam * ( 1 / dstNorm );

    // rotation (as a complex number) that takes srcSeam to dstSeam:
    // dstSeam * conj(srcSeam); applying it to d rotates d by the seam angle
    eCoord rot = dstSeam.Turn( srcSeam.Conj() );
    return d.Turn( rot );
}
