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

#include "nFeatures.h"
#include "nNetwork.h"

//! Minimum protocol version that introduces each named feature.
//! Surface-graph 3D maps arrive with protocol version 23 ("0.5_3dmaps").
static nVersionFeature sg_feature3D( 23 );

bool nFeatures::Supported( Flag flag )
{
    switch ( flag )
    {
    case THREE_D:
        return sg_feature3D.Supported();
    default:
        return false;
    }
}

bool nFeatures::ThreeDActive()
{
    return Supported( THREE_D );
}
