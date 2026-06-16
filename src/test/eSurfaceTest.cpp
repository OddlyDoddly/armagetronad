// Unit test for the surface-graph data model (eSurface / ePortal / eWorld) and
// the 3D feature gate (nFeatures), introduced by the map-system 3D rework.
//
// These exercise the new logic without the full XML/resource/game-init path:
// footprint point-in-polygon, per-level floor lookup, ramp->floor portal
// stitching, ramp z-interpolation, and portal frame mapping.

#include "eSurface.h"
#include "eWorld.h"
#include "nFeatures.h"
#include "tMemManager.h"

#include <iostream>
#include <vector>
#include <cmath>

static int failures = 0;

static void Check( bool cond, const char * msg )
{
    if ( cond )
        std::cout << "ok:   " << msg << "\n";
    else
    {
        std::cout << "FAIL: " << msg << "\n";
        ++failures;
    }
}

int main()
{
    // a fresh world wraps a single ground surface (id 0); a NULL grid is fine
    // here because these checks never touch grid topology.
    tJUST_CONTROLLED_PTR< eWorld > world = tNEW( eWorld )( (eGrid *)NULL );
    Check( world->SurfaceCount() == 1, "fresh world has exactly the ground surface" );
    Check( world->GroundSurface() != NULL && world->GroundSurface()->Id() == 0,
           "ground surface has id 0" );

    // ground floor: full 500x500 square
    eSurface * ground = world->GroundSurface();
    ground->SetLevels( eZLevel::Ground, eZLevel::Ground );
    std::vector< eCoord > gfp;
    gfp.push_back( eCoord( 0, 0 ) );
    gfp.push_back( eCoord( 500, 0 ) );
    gfp.push_back( eCoord( 500, 500 ) );
    gfp.push_back( eCoord( 0, 500 ) );
    ground->SetFootprint( gfp );
    Check( ground->ContainsPoint( eCoord( 250, 250 ) ), "ground footprint contains its center" );
    Check( !ground->ContainsPoint( eCoord( 600, 250 ) ), "ground footprint excludes outside point" );

    // air platform overlapping the ground in (x,y): a true vertical overhang
    eSurface * air = world->CreateSurface( eSurfaceKind::Floor );
    air->SetLevels( eZLevel::Air, eZLevel::Air );
    std::vector< eCoord > afp;
    afp.push_back( eCoord( 100, 100 ) );
    afp.push_back( eCoord( 400, 100 ) );
    afp.push_back( eCoord( 400, 400 ) );
    afp.push_back( eCoord( 100, 400 ) );
    air->SetFootprint( afp );
    Check( world->SurfaceCount() == 2, "world has two surfaces after adding the air floor" );

    // the same (x,y) resolves to different surfaces per level (overhang lookup)
    Check( world->FloorAt( eZLevel::Ground, eCoord( 250, 250 ) ) == ground,
           "FloorAt(ground) returns the ground floor" );
    Check( world->FloorAt( eZLevel::Air, eCoord( 250, 250 ) ) == air,
           "FloorAt(air) returns the air floor at the same (x,y)" );

    // ramp from ground up to air; seams chosen to land on each floor
    eSurface * ramp = world->CreateSurface( eSurfaceKind::Ramp );
    ramp->SetLevels( eZLevel::Ground, eZLevel::Air );
    int portals = world->StitchRamp(
        ramp,
        eCoord( 250, 150 ), eCoord( 250, 350 ), eZLevel::Ground,   // low seam, mid (250,250) on ground
        eCoord( 350, 150 ), eCoord( 350, 350 ), eZLevel::Air );    // high seam, mid (350,250) on air
    Check( portals == 2, "ramp stitched a portal at each end" );
    Check( world->PortalCount() == 2, "world records two portals" );
    Check( ramp->Portals().size() == 2, "ramp references both portals" );

    // ramp z-interpolation: gradient of 2 per x-unit
    ramp->SetZMap( 0.0, eCoord( 2.0, 0.0 ) );
    Check( std::fabs( ramp->ZAt( eCoord( 10, 0 ) ) - 20.0 ) < 1e-3, "ramp ZAt interpolates linearly" );

    // identity-frame portal maps a seam point back onto itself
    ePortal * p = ramp->Portals()[0];
    eCoord mapped = p->MapPoint( p->A(), eCoord( 250, 250 ) );
    Check( ( mapped - eCoord( 250, 250 ) ).Norm() < 1e-2, "portal MapPoint round-trips on identity frame" );

    // 3D feature gate is queryable (value depends on negotiated session state)
    std::cout << "info: nFeatures::ThreeDActive() = "
              << ( nFeatures::ThreeDActive() ? "true" : "false" ) << "\n";

    std::cout << ( failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n" );
    return failures == 0 ? 0 : 1;
}
