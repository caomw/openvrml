// -*- Mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; -*-
//
// OpenVRML
//
// Copyright (C) 1998  Chris Morley
// 
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
// 

# ifndef IMAGE_H
#   define IMAGE_H

# include <stddef.h>
# include "common.h"

namespace OpenVRML {

    class Doc;
    class Doc2;
    class MFString;

    /**
     * The Image class is a format-independent API for loading textures.
     */
    class OPENVRML_SCOPE Image {
        Doc * d_url;
        int d_w, d_h, d_nc, d_nFrames;
        unsigned char *d_pixels;
        unsigned char **d_frame;

    public:
        Image();
        ~Image();

        bool setURL(const char * url, const Doc2 * relative = 0);

        bool tryURLs(const MFString & urls, const Doc2 * relative = 0);

        const char *url();

        int w()			{ return d_w; }
        int h()			{ return d_h; }
        int nc()			{ return d_nc; }
        int nFrames()			{ return d_nFrames; }
        unsigned char *pixels()	{ return d_pixels; }
        unsigned char *pixels(int frame);

        void setSize(int w, int h)	{ d_w = w; d_h = h; }
    };
}

#endif
