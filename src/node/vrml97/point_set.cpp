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

# include "point_set.h"
# include <private.h>
# include <openvrml/node_impl_util.h>
# include <openvrml/viewer.h>
# include <boost/array.hpp>

# ifdef HAVE_CONFIG_H
#   include <config.h>
# endif

namespace {

    class OPENVRML_LOCAL point_set_node :
        public openvrml::node_impl_util::abstract_node<point_set_node>,
        public openvrml::geometry_node {

        friend class openvrml_node_vrml97::point_set_metatype;

        exposedfield<openvrml::sfnode> color_;
        exposedfield<openvrml::sfnode> coord_;

        openvrml::bounding_sphere bsphere;

    public:
        point_set_node(const openvrml::node_type & type,
                       const boost::shared_ptr<openvrml::scope> & scope);
        virtual ~point_set_node() OPENVRML_NOTHROW;

    private:
        virtual bool do_modified() const
            OPENVRML_THROW1(boost::thread_resource_error);

        virtual const openvrml::bounding_volume &
        do_bounding_volume() const;

        virtual void do_render_geometry(openvrml::viewer & viewer,
                                        openvrml::rendering_context context);
        virtual bool do_emissive() const OPENVRML_NOTHROW;

        void recalc_bsphere();
    };

    /**
     * @class point_set_node
     *
     * @brief Represents PointSet node instances.
     */

    /**
     * @var class point_set_node::point_set_metatype
     *
     * @brief Class object for PointSet instances.
     */

    /**
     * @var openvrml::node_impl_util::abstract_node<point_set_node>::exposedfield<openvrml::sfnode> point_set_node::color_
     *
     * @brief color exposedField.
     */

    /**
     * @var openvrml::node_impl_util::abstract_node<point_set_node>::exposedfield<openvrml::sfnode> point_set_node::coord_
     *
     * @brief coord exposedField.
     */

    /**
     * @var openvrml::bounding_sphere point_set_node::bsphere
     *
     * @brief Bounding volume.
     */

    /**
     * @brief Construct.
     *
     * @param type  the node_type associated with the node.
     * @param scope the scope to which the node belongs.
     */
    point_set_node::
    point_set_node(const openvrml::node_type & type,
                   const boost::shared_ptr<openvrml::scope> & scope):
        node(type, scope),
        bounded_volume_node(type, scope),
        openvrml::node_impl_util::abstract_node<point_set_node>(type, scope),
        geometry_node(type, scope),
        color_(*this),
        coord_(*this)
    {
        this->bounding_volume_dirty(true);
    }

    /**
     * @brief Destroy.
     */
    point_set_node::~point_set_node() OPENVRML_NOTHROW
    {}

    /**
     * @brief Determine whether the node has been modified.
     *
     * @return @c true if the node or one of its children has been modified,
     *      @c false otherwise.
     */
    bool point_set_node::do_modified() const
        OPENVRML_THROW1(boost::thread_resource_error)
    {
        return (this->color_.value() && this->color_.value()->modified())
            || (this->coord_.value() && this->coord_.value()->modified());
    }

    /**
     * @brief Insert this geometry into @p viewer's display list.
     *
     * @param viewer    a @c viewer.
     * @param context   the rendering context.
     */
    void
    point_set_node::
    do_render_geometry(openvrml::viewer & viewer,
                       const openvrml::rendering_context context)
    {
        using std::vector;
        using openvrml::node_cast;
        using openvrml::vec3f;

        if (context.draw_bounding_spheres) {
            using boost::polymorphic_downcast;
            using openvrml::bounding_sphere;
            using openvrml::bounding_volume;
            const bounding_sphere & bs =
                *polymorphic_downcast<const bounding_sphere *>(
                    &this->bounding_volume());
            viewer.draw_bounding_sphere(bs, bounding_volume::intersection(4));
        }

        openvrml::coordinate_node * const coordinateNode =
            node_cast<openvrml::coordinate_node *>(
                this->coord_.sfnode::value().get());
        const vector<vec3f> & coord = coordinateNode
            ? coordinateNode->point()
            : vector<vec3f>();

        openvrml::color_node * const colorNode =
            node_cast<openvrml::color_node *>(
                this->color_.sfnode::value().get());
        const vector<openvrml::color> & color = colorNode
            ? colorNode->color()
            : vector<openvrml::color>();

        viewer.insert_point_set(*this, coord, color);

        if (colorNode) { colorNode->modified(false); }
        if (coordinateNode) { coordinateNode->modified(false); }
    }

    /**
     * @brief Indicate that point sets should be drawn with the emissive color.
     *
     * @return @c true.
     */
    bool point_set_node::do_emissive() const OPENVRML_NOTHROW
    {
        return true;
    }

    /**
     * @brief Recalculate the bounding volume.
     */
    void point_set_node::recalc_bsphere()
    {
        using openvrml::node_cast;
        using openvrml::vec3f;
        this->bsphere = openvrml::bounding_sphere();
        openvrml::coordinate_node * const coordinateNode =
            node_cast<openvrml::coordinate_node *>(
                this->coord_.sfnode::value().get());
        if (coordinateNode) {
            const std::vector<vec3f> & coord = coordinateNode->point();
            for(std::vector<vec3f>::const_iterator vec(coord.begin());
                vec != coord.end(); ++vec) {
                this->bsphere.extend(*vec);
            }
        }
        this->bounding_volume_dirty(false);
    }

    /**
     * @brief Get the bounding volume.
     *
     * @return the bounding volume associated with the node.
     */
    const openvrml::bounding_volume &
    point_set_node::do_bounding_volume() const
    {
        if (this->bounding_volume_dirty()) {
            const_cast<point_set_node *>(this)->recalc_bsphere();
        }
        return this->bsphere;
    }
}


/**
 * @brief @c node_metatype identifier.
 */
const char * const openvrml_node_vrml97::point_set_metatype::id =
    "urn:X-openvrml:node:PointSet";

/**
 * @brief Construct.
 *
 * @param browser the @c browser associated with this @c node_metatype.
 */
openvrml_node_vrml97::point_set_metatype::
point_set_metatype(openvrml::browser & browser):
    node_metatype(point_set_metatype::id, browser)
{}

/**
 * @brief Destroy.
 */
openvrml_node_vrml97::point_set_metatype::~point_set_metatype()
    OPENVRML_NOTHROW
{}

# define POINT_SET_INTERFACE_SEQ                    \
    ((exposedfield, sfnode,  "color",    color_))   \
    ((exposedfield, sfnode,  "coord",    coord_))   \
    ((exposedfield, sfnode,  "metadata", metadata))

OPENVRML_NODE_IMPL_UTIL_DEFINE_DO_CREATE_TYPE(openvrml_node_vrml97,
                                              point_set_metatype,
                                              point_set_node,
                                              POINT_SET_INTERFACE_SEQ)
