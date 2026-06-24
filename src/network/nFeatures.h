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

#ifndef ArmageTron_FEATURES_H
#define ArmageTron_FEATURES_H

//! Named, negotiated capability flags broadcast between client and server.
//!
//! This is a thin, human-readable layer over the existing protocol-version
//! negotiation (nVersion / nVersionFeature). A feature is "active" only when
//! every connected peer supports it; otherwise the session degrades to the
//! behaviour of the common (older) protocol. This lets new servers advertise
//! capabilities by name while the wire format stays version-gated.
//!
//! For now each feature simply maps to a minimum protocol version. Optional
//! features (like THREE_D) negotiate down to the 2D protocol when a peer lacks
//! them; a feature can be made mandatory (rejecting the connection) by listing
//! it in the mandatory set -- this reuses the existing version-mismatch reject
//! path and needs no separate wire message.
class nFeatures
{
public:
    //! the named capabilities this build knows about
    enum Flag
    {
        THREE_D = 0,   //!< surface-graph 3D maps: Z-levels, ramps, overhangs
        FLAG_MAX
    };

    //! true iff the given feature is supported by everyone in the session
    static bool Supported( Flag flag );

    //! convenience: is the surface-graph 3D feature negotiated on for everyone?
    static bool ThreeDActive();
};

#endif // ArmageTron_FEATURES_H
