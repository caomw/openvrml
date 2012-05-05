// -*- Mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 78 -*-
//
// OpenVRML
//
// Copyright 1998  Chris Morley
// Copyright 2001, 2002, 2003, 2004, 2005, 2006, 2007  Braden McDaniel
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
# include <private.h>
# include "lod.h"
# include "grouping_node_base.h"

namespace {

    class OPENVRML_LOCAL lod_node :
        public openvrml_node_vrml97::grouping_node_base<lod_node> {

        friend class openvrml_node_vrml97::lod_metatype;

        openvrml::sfvec3f center_;
        openvrml::mffloat range_;

        openvrml::mfnode current_children_;

    public:
        lod_node(const openvrml::node_type & type,
                 const boost::shared_ptr<openvrml::scope> & scope);
        virtual ~lod_node() OPENVRML_NOTHROW;

    private:
        virtual bool do_modified() const
            OPENVRML_THROW1(boost::thread_resource_error);

         virtual void do_render_child(openvrml::viewer & viewer,
                                     openvrml::rendering_context context);
        virtual const std::vector<boost::intrusive_ptr<openvrml::node> >
            do_children() const OPENVRML_THROW1(std::bad_alloc);
        virtual void recalc_bsphere();
    };

    /**
     * @class lod_node
     *
     * @brief Represents LOD node instances.
     */

    /**
     * @var class lod_node::lod_metatype
     *
     * @brief Class object for LOD nodes.
     */

    /**
     * @var openvrml::sfvec3f lod_node::center_
     *
     * @brief center field.
     */

    /**
     * @var openvrml::mffloat lod_node::range_
     *
     * @brief range field.
     */

    /**
     * @var openvrml::mfnode lod_node::current_children_
     *
     * @brief Caches the active level for fast access by lod_node::children.
     */

    /**
     * @brief Construct.
     *
     * @param type  the node_type associated with this node.
     * @param scope the scope to which the node belongs.
     */
    lod_node::
    lod_node(const openvrml::node_type & type,
             const boost::shared_ptr<openvrml::scope> & scope):
        node(type, scope),
        bounded_volume_node(type, scope),
        child_node(type, scope),
        grouping_node(type, scope),
        openvrml_node_vrml97::grouping_node_base<lod_node>(type, scope),
        current_children_(1)
    {
        this->bounding_volume_dirty(true); // lazy calc of bvolume
    }

    /**
     * @brief Destroy.
     */
    lod_node::~lod_node() OPENVRML_NOTHROW
    {}

    /**
     * @brief Determine whether the node has been modified.
     *
     * @return @c true if the node or one of its children has been modified,
     *         @c false otherwise.
     */
    bool lod_node::do_modified() const
        OPENVRML_THROW1(boost::thread_resource_error)
    {
        return !this->current_children_.value().empty()
            && this->current_children_.value()[0]->modified();
    }

    /**
     * @brief Render the node.
     *
     * Render one of the children.
     *
     * @param viewer    a @c viewer.
     * @param context   a rendering context.
     */
    void lod_node::do_render_child(openvrml::viewer & viewer,
                                   const openvrml::rendering_context context)
    {
        using openvrml::mat4f;
        using openvrml::vec3f;
        using openvrml::make_vec3f;
        using boost::intrusive_ptr;
        using std::vector;

        if (this->children_.value().empty()) { return; }

        const mat4f modelview = context.matrix().inverse();
        vec3f v = make_vec3f(modelview[3][0], modelview[3][1], modelview[3][2]);
        v -= this->center_.value();
        const float d2 = v.dot(v);

        size_t i;
        if (this->range_.value().empty()) {
            i = this->children_.value().size() - 1;
        } else {
            for (i = 0; i < this->range_.value().size(); ++i) {
                if (d2 < this->range_.value()[i] * this->range_.value()[i]) {
                    break;
                }
            }
        }

        // Not enough levels...
        if (i >= this->children_.value().size()) {
            i = this->children_.value().size() - 1;
        }

        vector<intrusive_ptr<openvrml::node> > current_child(1);
        current_child[0] = this->children_.value()[i];
        this->current_children_.value(current_child);

        child_node * const child =
            openvrml::node_cast<child_node *>(current_child[0].get());
        if (child) { child->render_child(viewer, context); }

        current_child[0]->modified(false);
    }

    /**
     * @brief Get the children in the scene graph.
     *
     * @return the child nodes in the scene graph.
     *
     * @exception std::bad_alloc    if memory allocation fails.
     */
    const std::vector<boost::intrusive_ptr<openvrml::node> >
    lod_node::do_children() const OPENVRML_THROW1(std::bad_alloc)
    {
        return this->current_children_.value();
    }

    /**
     * @brief Recalculate the bounding volume.
     */
    void lod_node::recalc_bsphere()
    {
        this->bsphere = openvrml::bounding_sphere();

        // let's say our bsphere is the union of the bspheres of all the
        // levels. we could have said it was just the bsphere of the current
        // level, but the current level depends on the viewer position, and
        // we'd like to make the calculation idependent of that. we could do
        // some sort of trick where we reset the bsphere during render, but
        // that seems like overkill unless this simpler method proves to be
        // a bottleneck.
        //
        // hmm: just thought of a problem: one of the uses of the lod is to
        // switch in delayed-load inlines. this would necessarily switch
        // them in all at once. live with it for now.
        //
        for (size_t i = 0; i < this->children_.mfnode::value().size(); i++) {
            using openvrml::node_cast;
            const boost::intrusive_ptr<node> & node = this->children_.mfnode::value()[i];
            bounded_volume_node * bounded_volume =
                node_cast<bounded_volume_node *>(node.get());
            if (bounded_volume) {
                const openvrml::bounding_volume & ci_bv =
                    bounded_volume->bounding_volume();
                this->bsphere.extend(ci_bv);
            }
        }
        this->bounding_volume_dirty(false);
    }
}


/**
 * @brief @c node_metatype identifier.
 */
const char * const openvrml_node_vrml97::lod_metatype::id =
    "urn:X-openvrml:node:LOD";

/**
 * @brief Construct.
 *
 * @param browser the @c browser associated with this @c node_metatype object.
 */
openvrml_node_vrml97::lod_metatype::lod_metatype(openvrml::browser & browser):
    node_metatype(lod_metatype::id, browser)
{}

/**
 * @brief Destroy.
 */
openvrml_node_vrml97::lod_metatype::~lod_metatype() OPENVRML_NOTHROW
{}

/**
 * @brief Create a node_type.
 *
 * @param id            the name for the new node_type.
 * @param interfaces    the interfaces for the new node_type.
 *
 * @return a boost::shared_ptr<node_type> to a node_type capable of
 *         creating LOD nodes.
 *
 * @exception openvrml::unsupported_interface if @p interfaces includes an interface
 *                                  not supported by lod_metatype.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const boost::shared_ptr<openvrml::node_type>
openvrml_node_vrml97::lod_metatype::
do_create_type(const std::string & id,
               const openvrml::node_interface_set & interfaces) const
    OPENVRML_THROW2(openvrml::unsupported_interface, std::bad_alloc)
{
    using namespace openvrml;
    using namespace openvrml::node_impl_util;

    typedef boost::array<node_interface, 9> supported_interfaces_t;
    static const supported_interfaces_t supported_interfaces = {
        node_interface(node_interface::exposedfield_id,
                       field_value::mfnode_id,
                       "level"),
        node_interface(node_interface::field_id,
                       field_value::sfvec3f_id,
                       "center"),
        node_interface(node_interface::field_id,
                       field_value::mffloat_id,
                       "range"),
        node_interface(node_interface::exposedfield_id,
                       field_value::sfnode_id,
                       "metadata"),
        node_interface(node_interface::eventin_id,
                       field_value::mfnode_id,
                       "addChildren"),
        node_interface(node_interface::eventin_id,
                       field_value::mfnode_id,
                       "removeChildren"),
        node_interface(node_interface::exposedfield_id,
                       field_value::mfnode_id,
                       "children"),
        node_interface(node_interface::field_id,
                       field_value::sfvec3f_id,
                       "bboxCenter"),
        node_interface(node_interface::field_id,
                       field_value::sfvec3f_id,
                       "bboxSize")
    };

    typedef node_impl_util::node_type_impl<lod_node> node_type_t;

    const boost::shared_ptr<node_type> type(new node_type_t(*this, id));
    node_type_t & lodNodeType = static_cast<node_type_t &>(*type);
    for (node_interface_set::const_iterator interface_(interfaces.begin());
         interface_ != interfaces.end();
         ++interface_) {
        supported_interfaces_t::const_iterator supported_interface =
            supported_interfaces.begin() - 1;
        if (*interface_ == *++supported_interface) {
            lodNodeType.add_exposedfield(
                supported_interface->field_type,
                supported_interface->id,
                &lod_node::children_);
        } else if (*interface_ == *++supported_interface) {
            lodNodeType.add_field(
                supported_interface->field_type,
                supported_interface->id,
                &lod_node::center_);
        } else if (*interface_ == *++supported_interface) {
            lodNodeType.add_field(
                supported_interface->field_type,
                supported_interface->id,
                &lod_node::range_);
        } else if (*interface_ == *++supported_interface) {
            lodNodeType.add_exposedfield(
                supported_interface->field_type,
                supported_interface->id,
                &lod_node::metadata);
        } else if (*interface_ == *++supported_interface) {
            lodNodeType.add_eventin(
                supported_interface->field_type,
                supported_interface->id,
                &lod_node::add_children_listener_);
        } else if (*interface_ == *++supported_interface) {
            lodNodeType.add_eventin(
                supported_interface->field_type,
                supported_interface->id,
                &lod_node::remove_children_listener_);
        } else if (*interface_ == *++supported_interface) {
            lodNodeType.add_exposedfield(
                supported_interface->field_type,
                supported_interface->id,
                &lod_node::children_);
        } else if (*interface_ == *++supported_interface) {
            lodNodeType.add_field(
                supported_interface->field_type,
                supported_interface->id,
                &lod_node::bbox_center_);
        } else if (*interface_ == *++supported_interface) {
            lodNodeType.add_field(
                supported_interface->field_type,
                supported_interface->id,
                &lod_node::bbox_size_);
        } else {
            throw unsupported_interface(*interface_);
        }
    }
    return type;
}
