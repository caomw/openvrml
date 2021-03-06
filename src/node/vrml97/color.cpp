// -*- Mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 78 -*-
//
// OpenVRML
//
// Copyright 1998  Chris Morley
// Copyright 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2009  Braden McDaniel
//
// This library is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 3 of the License, or (at your
// option) any later version.
//
// This library is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
// License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this library; if not, see <http://www.gnu.org/licenses/>.
//

# ifdef HAVE_CONFIG_H
#   include <config.h>
# endif

# include <boost/array.hpp>
# include <openvrml/node_impl_util.h>
# include "color.h"

namespace {

    class OPENVRML_LOCAL color_node :
        public openvrml::node_impl_util::abstract_node<color_node>,
        public openvrml::color_node {

        friend class openvrml_node_vrml97::color_metatype;

        exposedfield<openvrml::mfcolor> color_;

    public:
        color_node(const openvrml::node_type & type,
                   const boost::shared_ptr<openvrml::scope> & scope);
        virtual ~color_node() OPENVRML_NOTHROW;

    private:
        //
        // color_node implementation
        //
        virtual const std::vector<openvrml::color> & do_color() const
            OPENVRML_NOTHROW;
    };

    /**
     * @class color_node
     *
     * @brief Color node instances.
     */

    /**
     * @var class color_node::color_metatype
     *
     * @brief Class object for Color nodes.
     */

    /**
     * @var openvrml::node_impl_util::abstract_node<color_node>::exposedfield<openvrml::mfcolor> color_node::color_
     *
     * @brief color exposedField.
     */

    /**
     * @brief Construct.
     *
     * @param type  the node_type associated with this node.
     * @param scope the scope to which the node belongs.
     */
    color_node::
    color_node(const openvrml::node_type & type,
               const boost::shared_ptr<openvrml::scope> & scope):
        node(type, scope),
        openvrml::node_impl_util::abstract_node<color_node>(type, scope),
        openvrml::color_node(type, scope),
        color_(*this)
    {}

    /**
     * @brief Destroy.
     */
    color_node::~color_node() OPENVRML_NOTHROW
    {}

    /**
     * @brief Get the color array.
     *
     * @return the color array associated with the node.
     */
    const std::vector<openvrml::color> & color_node::do_color() const
        OPENVRML_NOTHROW
    {
        return this->color_.mfcolor::value();
    }
}


/**
 * @brief @c node_metatype identifier.
 */
const char * const openvrml_node_vrml97::color_metatype::id =
    "urn:X-openvrml:node:Color";

/**
 * @brief Construct.
 *
 * @param browser the @c browser associated with this @c node_metatype object.
 */
openvrml_node_vrml97::color_metatype::
color_metatype(openvrml::browser & browser):
    node_metatype(color_metatype::id, browser)
{}

/**
 * @brief Destroy.
 */
openvrml_node_vrml97::color_metatype::~color_metatype() OPENVRML_NOTHROW
{}

# define COLOR_INTERFACE_SEQ                            \
    ((exposedfield, mfcolor, "color",    color_))       \
    ((exposedfield, sfnode,  "metadata", metadata))

OPENVRML_NODE_IMPL_UTIL_DEFINE_DO_CREATE_TYPE(openvrml_node_vrml97,
                                              color_metatype,
                                              color_node,
                                              COLOR_INTERFACE_SEQ)
