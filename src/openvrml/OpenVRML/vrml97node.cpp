//
// OpenVRML
//
// Copyright (C) 1998  Chris Morley
// Copyright (C) 1999  Kumaran Santhanam
// Copyright (C) 2001, 2002  Braden McDaniel
// Copyright (C) 2002  S. K. Bose
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

# ifdef HAVE_CONFIG_H
#   include <config.h>
# endif

# include <algorithm>
# include <iostream>
# include <iterator>
# ifdef OPENVRML_ENABLE_TEXT_NODE
#   include <ft2build.h>
#   include FT_FREETYPE_H
#   include FT_GLYPH_H
#   include FT_OUTLINE_H
#   include <fontconfig/fontconfig.h>
extern "C" {
#   include <fontconfig/fcfreetype.h>
}
# endif
# include "vrml97node.h"
# include "Doc.h"
# include "doc2.hpp"
# include "browser.h"
# include "Audio.h"
# include "sound.h"
# include "private.h"
# include "MathUtils.h"
# include "System.h"

/**
 * @internal
 */
namespace std {

    /**
     * @internal
     */
    template <>
    struct char_traits<unsigned char> {

        typedef unsigned char char_type;
        typedef int int_type;
        typedef streampos pos_type;
        typedef streamoff off_type;
        typedef mbstate_t state_type;

        static void assign(char_type & c1, const char_type & c2)
        {
            c1 = c2;
        }

        static bool eq(const char_type & c1, const char_type & c2)
        {
            return c1 == c2;
        }

        static bool lt(const char_type & c1, const char_type & c2)
        {
            return c1 < c2;
        }

        static int compare(const char_type * s1, const char_type * s2, size_t n)
        {
            for (size_t i = 0; i < n; ++i) {
                if (!eq(s1[i], s2[i])) { return lt(s1[i], s2[i]) ? -1 : 1; }
            }
            return 0;
        }

        static size_t length(const char_type * s)
        {
            const char_type * p = s;
            while (*p) { ++p; }
            return (p - s);
        }

        static const char_type * find(const char_type * s, size_t n,
                                      const char_type & a)
        {
            for (const char_type * p = s; size_t(p - s) < n; ++p) {
                if (*p == a) { return p; }
            }
            return 0;
        }

        static char_type * move(char_type * s1, const char_type * s2, size_t n)
        {
            return reinterpret_cast<char_type *>
                        (memmove(s1, s2, n * sizeof(char_type)));
        }

        static char_type * copy(char_type * s1, const char_type * s2, size_t n)
        {
            return reinterpret_cast<char_type *>
                        (memcpy(s1, s2, n * sizeof(char_type)));
        }

        static char_type * assign(char_type * s, size_t n, char_type a)
        {
            for (char_type * p = s; p < s + n; ++p) {
                assign(*p, a);
            }
            return s;
        }

        static int_type eof()
        {
            return static_cast<int_type>(-1);
        }

        static int_type not_eof(const int_type & c)
        {
            return eq_int_type(c, eof()) ? int_type(0) : c;
        }

        static char_type to_char_type(const int_type & e)
        {
            return char_type(e);
        }

        static int_type to_int_type(const char_type & c)
        {
            return int_type(c);
        }

        static bool eq_int_type(const int_type & e1, const int_type & e2)
        {
            return e1 == e2;
        }
    };
}

namespace OpenVRML {

/**
 * @brief Implementations of the VRML97 nodes.
 *
 * It is a legacy of OpenVRML's initial architecture that these classes are
 * exposed as part of the public API. Once all the dependencies on members of
 * this namespace have been factored out of other parts of OpenVRML, all of
 * these classes will be moving into the library's implementation.
 */
namespace Vrml97Node {

namespace {

    /**
     * @internal
     */
    class Vrml97NodeType : public NodeType {
    public:
        virtual ~Vrml97NodeType() throw () = 0;
        virtual void setFieldValue(Node & node, const std::string & id,
                                   const FieldValue &) const
            throw (UnsupportedInterface, std::bad_cast, std::bad_alloc) = 0;
        virtual const FieldValue & getFieldValue(const Node & node,
                                                 const std::string & id) const
            throw (UnsupportedInterface) = 0;
        virtual void dispatchEventIn(Node & node, const std::string & id,
                                     const FieldValue &, double timestamp) const
            throw (UnsupportedInterface, std::bad_cast, std::bad_alloc) = 0;
        virtual const FieldValue & getEventOutValue(const Node & node,
                                                    const std::string & id) const
            throw (UnsupportedInterface) = 0;

    protected:
        Vrml97NodeType(NodeClass & nodeClass, const std::string & id);
    };

    Vrml97NodeType::Vrml97NodeType(NodeClass & nodeClass,
                                   const std::string & id):
        NodeType(nodeClass, id)
    {}

    Vrml97NodeType::~Vrml97NodeType() throw ()
    {}


    template <typename NodeT>
    class NodeFieldPtr {
    public:
        virtual ~NodeFieldPtr() = 0;
        virtual FieldValue & dereference(NodeT & obj) = 0;
        virtual const FieldValue & dereference(const NodeT & obj) = 0;
    };

    template <typename NodeT>
    NodeFieldPtr<NodeT>::~NodeFieldPtr()
    {}


    template <typename NodeT, typename ConcreteFieldValue>
    class NodeFieldPtrImpl : public NodeFieldPtr<NodeT> {
        ConcreteFieldValue NodeT::* itsPtr;

    public:
        NodeFieldPtrImpl(ConcreteFieldValue NodeT::* ptr):
            itsPtr(ptr)
        {}

        virtual ~NodeFieldPtrImpl();

        virtual FieldValue & dereference(NodeT &);
        virtual const FieldValue & dereference(const NodeT &);
    };

    template <typename NodeT, typename ConcreteFieldValue>
    NodeFieldPtrImpl<NodeT, ConcreteFieldValue>::~NodeFieldPtrImpl()
    {}

    template <typename NodeT, typename ConcreteFieldValue>
    FieldValue &
    NodeFieldPtrImpl<NodeT, ConcreteFieldValue>::dereference(NodeT & obj)
    {
        return obj.*itsPtr;
    }

    template <typename NodeT, typename ConcreteFieldValue>
    const FieldValue &
    NodeFieldPtrImpl<NodeT, ConcreteFieldValue>::dereference(const NodeT & obj)
    {
        return obj.*itsPtr;
    }


    template <typename NodeT>
    class Vrml97NodeTypeImpl : public Vrml97NodeType {
    public:
        typedef OpenVRML_::SharedPtr<NodeFieldPtr<NodeT> > NodeFieldPtrPtr;
        typedef void (NodeT::* EventInHandlerPtr)(const FieldValue &, double);

    private:
        NodeInterfaceSet interfaces;
        typedef std::map<std::string, EventInHandlerPtr> EventInHandlerMap;
        typedef std::map<std::string, NodeFieldPtrPtr> FieldValueMap;
        mutable FieldValueMap fieldValueMap;
        EventInHandlerMap eventInHandlerMap;
        FieldValueMap eventOutValueMap;

    public:
        Vrml97NodeTypeImpl(NodeClass & nodeClass, const std::string & id);
        virtual ~Vrml97NodeTypeImpl() throw ();

        void addEventIn(FieldValue::Type, const std::string & id,
                        EventInHandlerPtr eventInHandlerPtr)
            throw (UnsupportedInterface, std::bad_alloc);
        void addEventOut(FieldValue::Type, const std::string & id,
                         const NodeFieldPtrPtr & eventOutPtrPtr)
            throw (UnsupportedInterface, std::bad_alloc);
        void addExposedField(FieldValue::Type, const std::string & id,
                             EventInHandlerPtr eventInHandlerPtr,
                             const NodeFieldPtrPtr & fieldPtrPtr)
            throw (UnsupportedInterface, std::bad_alloc);
        void addField(FieldValue::Type, const std::string & id,
                      const NodeFieldPtrPtr & fieldPtrPtr)
            throw (UnsupportedInterface, std::bad_alloc);

        virtual void setFieldValue(Node & node, const std::string & id,
                                   const FieldValue &) const
            throw (UnsupportedInterface, std::bad_cast, std::bad_alloc);
        virtual const FieldValue &
                getFieldValue(const Node & node,
                              const std::string & id) const
            throw (UnsupportedInterface);
        virtual void dispatchEventIn(Node & node,
                                     const std::string & id,
                                     const FieldValue &,
                                     double timestamp) const
            throw (UnsupportedInterface, std::bad_cast, std::bad_alloc);
        virtual const FieldValue &
                getEventOutValue(const Node & node,
                                 const std::string & id) const
            throw (UnsupportedInterface);

        virtual const NodeInterfaceSet & getInterfaces() const throw ();

        virtual const NodePtr createNode(const ScopePtr & scope) const
            throw (std::bad_alloc);

    private:
        void do_setFieldValue(NodeT & node, const std::string & id,
                              const FieldValue &) const
            throw (UnsupportedInterface, std::bad_cast, std::bad_alloc);
        const FieldValue & do_getFieldValue(const NodeT & node,
                                            const std::string & id) const
            throw (UnsupportedInterface);
        void do_dispatchEventIn(NodeT & node, const std::string & id,
                                 const FieldValue &, double timestamp) const
            throw (UnsupportedInterface, std::bad_cast, std::bad_alloc);
        const FieldValue & do_getEventOutValue(const NodeT & node,
                                                const std::string & id) const
            throw (UnsupportedInterface);
    };

    template <typename NodeT>
    Vrml97NodeTypeImpl<NodeT>::Vrml97NodeTypeImpl(NodeClass & nodeClass,
                                                  const std::string & id):
        Vrml97NodeType(nodeClass, id)
    {}

    template <typename NodeT>
    Vrml97NodeTypeImpl<NodeT>::~Vrml97NodeTypeImpl() throw ()
    {}

    template <typename NodeT>
    void Vrml97NodeTypeImpl<NodeT>::addEventIn(
            const FieldValue::Type type,
            const std::string & id,
            const EventInHandlerPtr eventInHandlerPtr)
        throw (UnsupportedInterface, std::bad_alloc)
    {
        const NodeInterface interface(NodeInterface::eventIn, type, id);
        this->interfaces.add(interface);
        const typename EventInHandlerMap::value_type
                value(id, eventInHandlerPtr);
        const bool succeeded = this->eventInHandlerMap.insert(value).second;
        assert(succeeded);
    }

    template <typename NodeT>
    void Vrml97NodeTypeImpl<NodeT>::addEventOut(
            const FieldValue::Type type,
            const std::string & id,
            const NodeFieldPtrPtr & eventOutPtrPtr)
        throw (UnsupportedInterface, std::bad_alloc)
    {
        const NodeInterface interface(NodeInterface::eventOut, type, id);
        this->interfaces.add(interface);
        const typename FieldValueMap::value_type value(id, eventOutPtrPtr);
        const bool succeeded = this->eventOutValueMap.insert(value).second;
        assert(succeeded);
    }

    template <typename NodeT>
    void Vrml97NodeTypeImpl<NodeT>::addExposedField(
            const FieldValue::Type type,
            const std::string & id,
            const EventInHandlerPtr eventInHandlerPtr,
            const NodeFieldPtrPtr & fieldPtrPtr)
        throw (UnsupportedInterface, std::bad_alloc)
    {
        const NodeInterface interface(NodeInterface::exposedField, type, id);
        this->interfaces.add(interface);

        bool succeeded;
        {
            const typename EventInHandlerMap::value_type
                    value("set_" + id, eventInHandlerPtr);
            succeeded = this->eventInHandlerMap.insert(value).second;
            assert(succeeded);
        }
        {
            const typename FieldValueMap::value_type value(id, fieldPtrPtr);
            succeeded = this->fieldValueMap.insert(value).second;
            assert(succeeded);
        }
        {
            const typename FieldValueMap::value_type
                    value(id + "_changed", fieldPtrPtr);
            succeeded = this->eventOutValueMap.insert(value).second;
            assert(succeeded);
        }
    }

    template <typename NodeT>
    void Vrml97NodeTypeImpl<NodeT>::addField(
            const FieldValue::Type type,
            const std::string & id,
            const NodeFieldPtrPtr & nodeFieldPtrPtr)
        throw (UnsupportedInterface, std::bad_alloc)
    {
        const NodeInterface interface(NodeInterface::field, type, id);
        this->interfaces.add(interface);
        const typename FieldValueMap::value_type value(id, nodeFieldPtrPtr);
        const bool succeeded = this->fieldValueMap.insert(value).second;
        assert(succeeded);
    }

    template <typename NodeT>
    void Vrml97NodeTypeImpl<NodeT>::setFieldValue(
            Node & node,
            const std::string & id,
            const FieldValue & newVal) const
        throw (UnsupportedInterface, std::bad_cast, std::bad_alloc)
    {
        assert(dynamic_cast<NodeT *>(&node));
        this->do_setFieldValue(dynamic_cast<NodeT &>(node), id, newVal);
    }

    template <typename NodeT>
    const FieldValue &
    Vrml97NodeTypeImpl<NodeT>::getFieldValue(const Node & node,
                                             const std::string & id) const
        throw (UnsupportedInterface)
    {
        assert(dynamic_cast<const NodeT *>(&node));
        return this->do_getFieldValue(dynamic_cast<const NodeT &>(node), id);
    }

    template <typename NodeT>
    void
    Vrml97NodeTypeImpl<NodeT>::dispatchEventIn(Node & node,
                                               const std::string & id,
                                               const FieldValue & value,
                                               const double timestamp) const
        throw (UnsupportedInterface, std::bad_cast, std::bad_alloc)
    {
        assert(dynamic_cast<NodeT *>(&node));
        this->do_dispatchEventIn(dynamic_cast<NodeT &>(node), id, value,
                                 timestamp);
    }

    template <typename NodeT>
    const FieldValue &
    Vrml97NodeTypeImpl<NodeT>::getEventOutValue(const Node & node,
                                                const std::string & id) const
        throw (UnsupportedInterface)
    {
        assert(dynamic_cast<const NodeT *>(&node));
        return this->do_getEventOutValue(dynamic_cast<const NodeT &>(node), id);
    }

    template <typename NodeT>
    const NodeInterfaceSet & Vrml97NodeTypeImpl<NodeT>::getInterfaces() const
        throw ()
    {
        return this->interfaces;
    }

    template <typename NodeT>
    const NodePtr
    Vrml97NodeTypeImpl<NodeT>::createNode(const ScopePtr & scope) const
        throw (std::bad_alloc)
    {
        return NodePtr(new NodeT(*this, scope));
    }

    template <typename NodeT>
    void Vrml97NodeTypeImpl<NodeT>::do_setFieldValue(
            NodeT & node,
            const std::string & id,
            const FieldValue & newVal) const
        throw (UnsupportedInterface, std::bad_cast, std::bad_alloc)
    {
        typename FieldValueMap::iterator itr = this->fieldValueMap.find(id);
        if (itr == this->fieldValueMap.end()) {
            throw UnsupportedInterface(node.nodeType, NodeInterface::field, id);
        }
        itr->second->dereference(node).assign(newVal);
    }

    template <typename NodeT>
    const FieldValue &
    Vrml97NodeTypeImpl<NodeT>::do_getFieldValue(const NodeT & node,
                                                const std::string & id) const
        throw (UnsupportedInterface)
    {
        const typename FieldValueMap::const_iterator itr =
                this->fieldValueMap.find(id);
        if (itr == this->fieldValueMap.end()) {
            throw UnsupportedInterface(node.nodeType, NodeInterface::field, id);
        }
        return itr->second->dereference(node);
    }

    template <typename NodeT>
    void Vrml97NodeTypeImpl<NodeT>::do_dispatchEventIn(
            NodeT & node,
            const std::string & id,
            const FieldValue & value,
            const double timestamp) const
        throw (UnsupportedInterface, std::bad_cast, std::bad_alloc)
    {
        typename EventInHandlerMap::const_iterator
                itr(this->eventInHandlerMap.find(id));
        if (itr == this->eventInHandlerMap.end()) {
            itr = this->eventInHandlerMap.find("set_" + id);
        }
        if (itr == this->eventInHandlerMap.end()) {
            throw UnsupportedInterface(node.nodeType,
                                       NodeInterface::eventIn,
                                       id);
        }
        (node.*(itr->second))(value, timestamp);
    }

    template <typename NodeT>
    const FieldValue &
    Vrml97NodeTypeImpl<NodeT>::do_getEventOutValue(
            const NodeT & node,
            const std::string & id) const
        throw (UnsupportedInterface)
    {
        typename FieldValueMap::const_iterator
                itr(this->eventOutValueMap.find(id));
        if (itr == this->eventOutValueMap.end()) {
            itr = this->eventOutValueMap.find(id + "_changed");
        }
        if (itr == this->eventOutValueMap.end()) {
            throw UnsupportedInterface(node.nodeType,
                                       NodeInterface::eventOut,
                                       id);
        }
        return itr->second->dereference(node);
    }
}

/**
 * @class AbstractBase
 *
 * @brief Abstract base class for VRML97 node implementations.
 *
 * AbstractBase encapsulates the mechanisms for field access and mutation,
 * event dispatch, and eventOut access.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with this node.
 * @param scope     the Scope to which the node belongs.
 */
AbstractBase::AbstractBase(const NodeType & nodeType, const ScopePtr & scope):
    Node(nodeType, scope)
{}

/**
 * @brief Destructor.
 */
AbstractBase::~AbstractBase() throw ()
{}

/**
 * @brief Set a field value for a node.
 *
 * @param id    a field name.
 * @param value a FieldValue.
 *
 * @exception UnsupportedInterface  if the node has no field @p id.
 * @exception std::bad_cast         if @p value is not the correct type.
 * @exception std::bad_alloc        if memory allocation fails.
 *
 * @pre @p value must be of the correct type.
 */
void AbstractBase::do_setField(const std::string & id,
                               const FieldValue & value)
    throw (UnsupportedInterface, std::bad_cast, std::bad_alloc)
{
    assert(dynamic_cast<const Vrml97NodeType *>(&this->nodeType));
    static_cast<const Vrml97NodeType &>(this->nodeType)
            .setFieldValue(*this, id, value);
}

/**
 * @brief Get a field value for a node.
 *
 * @param id    a field name.
 *
 * @exception UnsupportedInterface  if the node has no field @p id.
 */
const FieldValue & AbstractBase::do_getField(const std::string & id) const
    throw (UnsupportedInterface)
{
    assert(dynamic_cast<const Vrml97NodeType *>(&this->nodeType));
    return static_cast<const Vrml97NodeType &>(this->nodeType)
            .getFieldValue(*this, id);
}

/**
 * @brief Process an event.
 *
 * @param id        an eventIn name.
 * @param value     a FieldValue.
 * @param timestamp the current time.
 *
 * @exception UnsupportedInterface  if the node has no eventIn @p id.
 * @exception std::bad_cast         if @p value is not the correct type.
 * @exception std::bad_alloc        if memory allocation fails.
 *
 * @pre @p value must be of the correct type.
 */
void AbstractBase::do_processEvent(const std::string & id,
                                   const FieldValue & value,
                                   const double timestamp)
    throw (UnsupportedInterface, std::bad_cast, std::bad_alloc)
{
    assert(dynamic_cast<const Vrml97NodeType *>(&this->nodeType));
    static_cast<const Vrml97NodeType &>(this->nodeType)
            .dispatchEventIn(*this, id, value, timestamp);
}

/**
 * @brief Get an eventOut value for a node.
 *
 * @param id    an eventOut name.
 *
 * @exception UnsupportedInterface  if the node has no eventOut @p id.
 */
const FieldValue & AbstractBase::do_getEventOut(const std::string & id) const
    throw (UnsupportedInterface)
{
    assert(dynamic_cast<const Vrml97NodeType *>(&this->nodeType));
    return static_cast<const Vrml97NodeType &>(this->nodeType)
            .getEventOutValue(*this, id);
}


/**
 * @class AbstractChild
 *
 * @brief Base class for all child nodes.
 *
 * VRML97 defines "child" nodes as those that may occur at the root of the
 * scene, in the "children" field of a Group, Transform, Anchor, or Billboard
 * node, in the "level" field of a LOD node, or in the "choice" field of a
 * Switch node.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType for the node.
 * @param scope     the Scope the new node should belong to.
 */
AbstractChild::AbstractChild(const NodeType & nodeType, const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    ChildNode(nodeType, scope)
{}

/**
 * @brief Destructor.
 */
AbstractChild::~AbstractChild() throw ()
{}

/**
 * @class AbstractGeometry
 *
 * @brief Base class for all geometry nodes.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType for the node.
 * @param scope     the Scope the new node should belong to.
 */
AbstractGeometry::AbstractGeometry(const NodeType & nodeType,
                                   const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    GeometryNode(nodeType, scope),
    d_viewerObject(0)
{}

/**
 * @brief Destructor.
 */
AbstractGeometry::~AbstractGeometry() throw ()
{
    /* Need access to viewer to delete viewerObject...*/
}

/**
 * @brief Render this node.
 *
 * Subclasses need only define insertGeometry(), not render().
 *
 * @param viewer a renderer
 * @param context the renderer context
 */
void AbstractGeometry::render(Viewer & viewer, VrmlRenderContext context)
{
    if (this->d_viewerObject && this->isModified()) {
        viewer.removeObject(this->d_viewerObject);
        this->d_viewerObject = 0;
    }

    if (this->d_viewerObject) {
        viewer.insertReference(this->d_viewerObject);
    } else {
        this->d_viewerObject = this->insertGeometry(viewer, context);
        this->clearModified();
    }
}


/**
 * @class AbstractIndexedSet
 *
 * @brief Abstract base class for IndexedFaceSet and IndexedLineSet.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType      the NodeType associated with the instance.
 * @param scope         the Scope that the new node will belong to.
 */
AbstractIndexedSet::AbstractIndexedSet(const NodeType & nodeType,
                                       const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractGeometry(nodeType, scope),
    colorPerVertex(true)
{}

/**
 * @brief Destructor.
 */
AbstractIndexedSet::~AbstractIndexedSet() throw ()
{}

/**
 * @brief Determine whether the node has been modified.
 *
 * @return @c true if the node or one of its children has been modified,
 *      @c false otherwise.
 */
bool AbstractIndexedSet::isModified() const
{
    return (this->d_modified
            || (this->color.value && this->color.value->isModified())
            || (this->coord.value && this->coord.value->isModified()));
}

/**
 * @brief Propagate the bvolume dirty flag from children to parents.
 *
 * @param path  stack of ancestor nodes.
 * @param flags 1 indicates normal modified flag, 2 indicates the
 *              bvolume dirty flag, 3 indicates both.
 */
void AbstractIndexedSet::updateModified(NodePath & path, int flags)
{
    if (this->isModified()) { markPathModified(path, true); }
    path.push_front(this);
    if (this->color.value) { this->color.value->updateModified(path); }
    if (this->coord.value) { this->coord.value->updateModified(path); }
    path.pop_front();
}

const ColorNode * AbstractIndexedSet::getColor() const throw ()
{
    return this->color.value
            ? this->color.value->toColor()
            : 0;
}

/**
 * @brief set_color eventIn handler.
 *
 * @param sfnode    an SFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void AbstractIndexedSet::processSet_color(const FieldValue & sfnode,
                                          const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->color = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("color_changed", this->color, timestamp);
}

/**
 * @brief set_colorIndex eventIn handler.
 *
 * @param mfint32   an MFInt32 value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfint32 is not an MFInt32.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void AbstractIndexedSet::processSet_colorIndex(const FieldValue & mfint32,
                                               const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->colorIndex = dynamic_cast<const MFInt32 &>(mfint32);
    this->setModified();
}

/**
 * @brief set_coord eventIn handler.
 *
 * @param sfnode    an SFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void AbstractIndexedSet::processSet_coord(const FieldValue & sfnode,
                                          const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->coord = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("coord_changed", this->coord, timestamp);
}

/**
 * @brief set_coordIndex eventIn handler.
 *
 * @param mfint32   an MFInt32 value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfint32 is not an MFInt32.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void AbstractIndexedSet::processSet_coordIndex(const FieldValue & mfint32,
                                               const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->coordIndex = dynamic_cast<const MFInt32 &>(mfint32);
    this->setModified();
}


/**
 * @class AbstractLight
 *
 * @brief Base class for all light nodes.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType for the node.
 * @param scope     the Scope to which the node belongs.
 */
AbstractLight::AbstractLight(const NodeType & nodeType, const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope),
    ambientIntensity(0.0),
    color(OpenVRML::color(1.0, 1.0, 1.0)),
    intensity(1.0),
    on(true)
{}

/**
 * @brief Destructor.
 */
AbstractLight::~AbstractLight() throw ()
{}

/**
 * @brief Downcast to a light node.
 *
 * @return a pointer to this object.
 */
AbstractLight * AbstractLight::toLight() const
{
    return const_cast<AbstractLight *>(this);
}

/**
 * @brief Render this node as scoped.
 *
 * The default version of this method does nothing.
 *
 * @param viewer a renderer.
 */
void AbstractLight::renderScoped(Viewer & viewer)
{}

/**
 * @brief set_ambientIntensity eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void AbstractLight::processSet_ambientIntensity(const FieldValue & sffloat,
                                                const double timestamp)
    throw (std::bad_cast)
{
    this->ambientIntensity = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("ambientIntensity_changed", this->ambientIntensity,
                    timestamp);
}

/**
 * @brief set_color eventIn handler.
 *
 * @param sfcolor   an SFColor value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfcolor is not an SFColor.
 */
void AbstractLight::processSet_color(const FieldValue & sfcolor,
                                     const double timestamp)
    throw (std::bad_cast)
{
    this->color = dynamic_cast<const SFColor &>(sfcolor);
    this->setModified();
    this->emitEvent("color_changed", this->color, timestamp);
}

/**
 * @brief set_intensity eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void AbstractLight::processSet_intensity(const FieldValue & sffloat,
                                         const double timestamp)
    throw (std::bad_cast)
{
    this->intensity = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("intensity_changed", this->intensity, timestamp);
}

/**
 * @brief set_on eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void AbstractLight::processSet_on(const FieldValue & sfbool,
                                  const double timestamp)
    throw (std::bad_cast)
{
    this->on = dynamic_cast<const SFBool &>(sfbool);
    this->setModified();
    this->emitEvent("on_changed", this->on, timestamp);
}


/**
 * @class AbstractTexture
 *
 * @brief Abstract base class for VRML97 texture nodes.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType for the node instance.
 * @param scope     the Scope to which the node belongs.
 */
AbstractTexture::AbstractTexture(const NodeType & nodeType,
                                 const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    TextureNode(nodeType, scope),
    repeatS(true),
    repeatT(true)
{}

/**
 * @brief Destructor.
 */
AbstractTexture::~AbstractTexture() throw ()
{}

/**
 * @brief Get the repeatS flag.
 *
 * @return @c TRUE if the texture should repeat in the <var>S</var> direction,
 *      @c FALSE otherwise.
 */
bool AbstractTexture::getRepeatS() const throw ()
{
    return this->repeatS.value;
}

/**
 * @brief Get the repeatT flag.
 *
 * @return @c TRUE if the texture should repeat in the <var>T</var> direction,
 *      @c FALSE otherwise.
 */
bool AbstractTexture::getRepeatT() const throw ()
{
    return this->repeatT.value;
}


/**
 * @class AnchorClass.
 *
 * @brief Class object for Anchor nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this AnchorClass.
 */
AnchorClass::AnchorClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
AnchorClass::~AnchorClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Anchor nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by AnchorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr AnchorClass::createType(const std::string & id,
                                          const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::mfnode, "addChildren"),
        NodeInterface(NodeInterface::eventIn, FieldValue::mfnode, "removeChildren"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfnode, "children"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfstring, "description"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "parameter"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "url"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "bboxCenter"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "bboxSize")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Anchor>(*this, id));
    Vrml97NodeTypeImpl<Anchor> & anchorNodeType =
            static_cast<Vrml97NodeTypeImpl<Anchor> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Anchor>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            anchorNodeType.addEventIn(supportedInterfaces[0].fieldType,
                                      supportedInterfaces[0].id,
                                      &Anchor::processAddChildren);
        } else if (*itr == supportedInterfaces[1]) {
            anchorNodeType.addEventIn(supportedInterfaces[1].fieldType,
                                      supportedInterfaces[1].id,
                                      &Anchor::processRemoveChildren);
        } else if (*itr == supportedInterfaces[2]) {
            anchorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &Anchor::processSet_children,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Anchor, MFNode>
                                    (&Anchor::children)));
        } else if (*itr == supportedInterfaces[3]) {
            anchorNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &Anchor::processSet_description,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Anchor, SFString>
                                    (&Anchor::description)));
        } else if (*itr == supportedInterfaces[4]) {
            anchorNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &Anchor::processSet_parameter,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Anchor, MFString>
                                    (&Anchor::parameter)));
        } else if (*itr == supportedInterfaces[5]) {
            anchorNodeType.addExposedField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                &Anchor::processSet_url,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Anchor, MFString>
                                    (&Anchor::url)));
        } else if (*itr == supportedInterfaces[6]) {
            anchorNodeType.addField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Anchor, SFVec3f>
                                    (&Anchor::bboxCenter)));
        } else if (*itr == supportedInterfaces[7]) {
            anchorNodeType.addField(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Anchor, SFVec3f>
                                (&Anchor::bboxSize)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Anchor
 *
 * @brief Represents Anchor node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with this node.
 * @param scope     the Scope to which the node belongs.
 */
Anchor::Anchor(const NodeType & nodeType,
               const ScopePtr & scope):
    Node(nodeType, scope),
    ChildNode(nodeType, scope),
    GroupingNode(nodeType, scope),
    Group(nodeType, scope)
{
    this->setBVolumeDirty(true);
}

/**
 * @brief Destructor.
 */
Anchor::~Anchor() throw ()
{}

/**
 * @brief set_description eventIn handler.
 *
 * @param sfstring  an SFString value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfstring is not an SFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Anchor::processSet_description(const FieldValue & sfstring,
                                    const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->description = dynamic_cast<const SFString &>(sfstring);
    this->emitEvent("description_changed", sfstring, timestamp);
}

/**
 * @brief set_parameter eventIn handler.
 *
 * @param mfstring  an MFString value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Anchor::processSet_parameter(const FieldValue & mfstring,
                                  const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->parameter = dynamic_cast<const MFString &>(mfstring);
    this->emitEvent("parameter_changed", mfstring, timestamp);
}

/**
 * @brief set_url eventIn handler.
 *
 * @param mfstring  an MFString value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Anchor::processSet_url(const FieldValue & mfstring,
                            const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->url = dynamic_cast<const MFString &>(mfstring);
    this->emitEvent("url_changed", mfstring, timestamp);
}

/**
 * @brief Cast to an Anchor.
 *
 * @return a pointer to this node.
 */
Anchor * Anchor::toAnchor() const
{
    return const_cast<Anchor *>(this);
}

/**
 * @brief Render the node.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void Anchor::render(Viewer & viewer, const VrmlRenderContext context)
{
    viewer.setSensitive(this);

    // Render children
    this->Group::render(viewer, context);

    viewer.setSensitive(0);
}

/**
 * @brief Handle a click by loading the url.
 */
void Anchor::activate()
{
    assert(this->getScene());
    this->getScene()->loadURI(this->url.value, this->parameter.value);
}

/**
 * @brief Get the bounding volume.
 *
 * @return the bounding volume associated with the node.
 */
const BVolume * Anchor::getBVolume() const
{
    return Group::getBVolume();
}

/**
 * @class AppearanceClass
 *
 * @brief Class object for Appearance nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
AppearanceClass::AppearanceClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
AppearanceClass::~AppearanceClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Appearance nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by AppearanceClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
AppearanceClass::createType(const std::string & id,
                            const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "material"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "texture"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "textureTransform")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Appearance>(*this, id));
    Vrml97NodeTypeImpl<Appearance> & appearanceNodeType =
            static_cast<Vrml97NodeTypeImpl<Appearance> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Appearance>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            appearanceNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &Appearance::processSet_material,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Appearance, SFNode>
                                    (&Appearance::material)));
        } else if (*itr == supportedInterfaces[1]) {
            appearanceNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &Appearance::processSet_texture,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Appearance, SFNode>
                                    (&Appearance::texture)));
        } else if (*itr == supportedInterfaces[2]) {
            appearanceNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &Appearance::processSet_textureTransform,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Appearance, SFNode>
                                    (&Appearance::textureTransform)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Appearance
 *
 * @brief Appearance node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
Appearance::Appearance(const NodeType & nodeType,
                       const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    AppearanceNode(nodeType, scope)
{}

/**
 * @brief Destructor.
 */
Appearance::~Appearance() throw ()
{}

/**
 * @brief set_material eventIn handler.
 *
 * @param sfnode    an SFNode value; should be a Material node.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Appearance::processSet_material(const FieldValue & sfnode,
                                     double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->material = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("material_changed", sfnode, timestamp);
}

/**
 * @brief set_texture eventIn handler.
 *
 * @param sfnode    an SFNode value; should be a Texture node.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Appearance::processSet_texture(const FieldValue & sfnode,
                                    double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->texture = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("texture_changed", sfnode, timestamp);
}

/**
 * @brief set_textureTransform eventIn handler.
 *
 * @param sfnode    an SFNode value; should be a TextureTransform node.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Appearance::processSet_textureTransform(const FieldValue & sfnode,
                                             double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->textureTransform = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("textureTransform_changed", sfnode, timestamp);
}

/**
 * @brief Determine whether the node has been modified.
 *
 * @return @c true if the node or one of its children has been modified,
 *      @c false otherwise.
 */
bool Appearance::isModified() const
{
    return (this->d_modified
          || (this->material.value && this->material.value->isModified())
          || (this->texture.value && this->texture.value->isModified())
          || (this->textureTransform.value
                && this->textureTransform.value->isModified()));
}

/**
 * @brief Propagate the bvolume dirty flag from children to parents.
 *
 * @param path  stack of ancestor nodes.
 * @param flags 1 indicates normal modified flag, 2 indicates the
 *              bvolume dirty flag, 3 indicates both.
 */
void Appearance::updateModified(NodePath & path, int flags)
{
    if (this->isModified()) { markPathModified(path, true); }
    path.push_front(this);
    if (this->material.value) { this->material.value->updateModified(path); }
    if (this->texture.value) { this->texture.value->updateModified(path); }
    if (this->textureTransform.value) {
        this->textureTransform.value->updateModified(path);
    }
    path.pop_front();
}

/**
 * @brief Render the node.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void Appearance::render(Viewer & viewer, const VrmlRenderContext context)
{
    MaterialNode * const material = this->material.value
                                  ? this->material.value->toMaterial()
                                  : 0;
    TextureNode * const texture = this->texture.value
                                ? this->texture.value->toTexture()
                                : 0;

    if (material) {
        float trans = material->getTransparency();
        color diffuse = material->getDiffuseColor();
        size_t nTexComponents = texture ? texture->nComponents() : 0;
        if (nTexComponents == 2 || nTexComponents == 4) { trans = 0.0; }
        if (nTexComponents >= 3) { diffuse = color(1.0, 1.0, 1.0); }

        viewer.enableLighting(true);   // turn lighting on for this object
        viewer.setMaterial(material->getAmbientIntensity(),
                           diffuse,
                           material->getEmissiveColor(),
                           material->getShininess(),
                           material->getSpecularColor(),
                           trans);

        material->clearModified();
    } else {
        viewer.setColor(color(1.0, 1.0, 1.0)); // default color
        viewer.enableLighting(false);   // turn lighting off for this object
    }

    if (texture) {
        if (this->textureTransform.value) {
            this->textureTransform.value->render(viewer, context);
        } else {
            static const vec2f center(0.0, 0.0);
            static const float rotation = 0.0;
            static const vec2f scale(1.0, 1.0);
            static const vec2f translation(0.0, 0.0);
            viewer.setTextureTransform(center, rotation, scale, translation);
        }
        texture->render(viewer, context);
    }
    this->clearModified();
}

/**
 * @brief Get the material node.
 *
 * @returns an SFNode object containing the Material node associated with
 *          this Appearance.
 */
const NodePtr & Appearance::getMaterial() const throw ()
{
    return this->material.value;
}

/**
 * @brief Get the texture node.
 *
 * @return an SFNode object containing the texture node associated with
 *         this Appearance.
 */
const NodePtr & Appearance::getTexture() const throw ()
{
    return this->texture.value;
}

/**
 * @brief Get the texture transform node.
 *
 * @return an SFNode object containing the TextureTransform node
 *         associated with this Appearance.
 */
const NodePtr & Appearance::getTextureTransform() const throw ()
{
    return this->textureTransform.value;
}


/**
 * @class AudioClipClass
 *
 * @brief Class object for AudioClip nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
AudioClipClass::AudioClipClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
AudioClipClass::~AudioClipClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating AudioClip nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by AudioClipClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
AudioClipClass::createType(const std::string & id,
                           const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfstring, "description"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "loop"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "pitch"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sftime, "startTime"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sftime, "stopTime"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "url"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sftime, "duration_changed"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isActive")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<AudioClip>(*this, id));
    Vrml97NodeTypeImpl<AudioClip> & audioClipNodeType =
            static_cast<Vrml97NodeTypeImpl<AudioClip> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<AudioClip>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            audioClipNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &AudioClip::processSet_description,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<AudioClip, SFString>
                                    (&AudioClip::description)));
        } else if (*itr == supportedInterfaces[1]) {
            audioClipNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &AudioClip::processSet_loop,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<AudioClip, SFBool>
                                    (&AudioClip::loop)));
        } else if (*itr == supportedInterfaces[2]) {
            audioClipNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &AudioClip::processSet_pitch,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<AudioClip, SFFloat>
                                    (&AudioClip::pitch)));
        } else if (*itr == supportedInterfaces[3]) {
            audioClipNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &AudioClip::processSet_startTime,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<AudioClip, SFTime>
                                    (&AudioClip::startTime)));
        } else if (*itr == supportedInterfaces[4]) {
            audioClipNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &AudioClip::processSet_stopTime,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<AudioClip, SFTime>
                                    (&AudioClip::stopTime)));
        } else if (*itr == supportedInterfaces[5]) {
            audioClipNodeType.addExposedField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                &AudioClip::processSet_url,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<AudioClip, MFString>
                                    (&AudioClip::url)));
        } else if (*itr == supportedInterfaces[6]) {
            audioClipNodeType.addEventOut(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<AudioClip, SFTime>
                                    (&AudioClip::duration)));
        } else if (*itr == supportedInterfaces[7]) {
            audioClipNodeType.addEventOut(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<AudioClip, SFBool>
                                    (&AudioClip::active)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class AudioClip
 *
 * @brief AudioClip node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
AudioClip::AudioClip(const NodeType & nodeType,
                     const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    pitch(1.0),
    active(false),
    audio(0),
    url_modified(false),
    audio_index(0),
    audio_intensity(1.0),
    audio_fd(-1)
{}

/**
 * @brief Destructor.
 */
AudioClip::~AudioClip() throw ()
{
    delete this->audio;
}

/**
 * @brief Cast to an AudioClip node.
 *
 * @return a pointer to this node.
 */
AudioClip* AudioClip::toAudioClip() const
{
    return (AudioClip*)this;
}

void AudioClip::update(const double currentTime)
{
    // If the URL has been modified, update the audio object
    if (this->url_modified) {
        Doc relDoc(this->relativeUrl.value, static_cast<Doc const *>(0));
        delete this->audio;
        std::string emptyUrl;
        this->audio = new Audio(emptyUrl);
        if (this->audio->tryURLs(this->url, &relDoc)) {
            this->duration.value = this->audio->duration();
            this->emitEvent("duration_changed", this->duration, currentTime);
        } else {
#if HAVE_SOUND
            cerr << "Error: couldn't read AudioClip from URL "
                 << this->url << endl;
#endif
            delete this->audio;
            this->audio = 0;
        }

        this->url_modified = false;
    }

    // If there's no audio or START <= 0, we don't play anything
    if (this->audio == 0 || this->startTime.value <= 0)
        return;

    // Determine if this clip should be playing right now
    bool audible = false;

    // If START < STOP  and  START <= NOW < STOP
    if (this->stopTime.value > this->startTime.value)
        audible = (this->startTime.value <= currentTime &&
                   currentTime < this->stopTime.value);

    // If STOP < START  and  START <= NOW
    else
        audible = (currentTime >= this->startTime.value);

    // If the clip is not looping, it's not audible after
    // its duration has expired.
    if (!this->loop.value) {
        if (currentTime - this->startTime.value > this->audio->duration()) {
            audible = false;
        }
    }

    // If the clip is audible, play it.  Otherwise, stop it.
    if (audible) {
        // If the sound device is not already open, open it.
        if (this->audio_fd < 0) {
            this->audio_fd = openSound(this->audio->channels(),
                                       this->audio->bitsPerSample(),
                                       this->audio->samplesPerSec());

            // If we can't get a sound device, silently return
            if (this->audio_fd < 0) {
                return;
            }

            this->audio_index =
                this->audio->getByteIndex(currentTime
                                          - this->startTime.value);

            this->active.value = true;
            this->emitEvent("isActive", this->active, currentTime);
        }

        // Send out a sound buffer
        this->audio_index = outputSoundChunk(this->audio->numBytes(),
                                             this->audio->samples(),
                                             this->audio->bitsPerSample(),
                                             this->audio_index,
                                             this->loop.value,
                                             this->audio_intensity,
                                             this->audio_fd);
    }

    // Otherwise, close the sound device
    else {
        if (this->audio_fd >= 0) {
            this->audio_fd = closeSound(this->audio_fd);
            this->active.value = false;
            this->emitEvent("isActive", this->active, currentTime);
        }
    }
}

/**
 * @brief Initialize.
 *
 * @param timestamp the current time.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void AudioClip::do_initialize(const double timestamp) throw (std::bad_alloc)
{
    assert(this->getScene());
    this->getScene()->browser.addAudioClip(*this);
}

/**
 * @brief Shut down.
 *
 * @param timestamp the current time.
 */
void AudioClip::do_shutdown(const double timestamp) throw ()
{
    assert(this->getScene());
    this->getScene()->browser.removeAudioClip(*this);
}

/**
 * @brief set_description eventIn handler.
 *
 * @param sfstring  an SFString.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfstring is not an SFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void AudioClip::processSet_description(const FieldValue & sfstring,
                                       const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->description = dynamic_cast<const SFString &>(sfstring);
    this->emitEvent("description_changed", this->description, timestamp);
}

/**
 * @brief set_loop eventIn handler.
 *
 * @param sfbool    an SFBool.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfbool is not an SFBool.
 */
void AudioClip::processSet_loop(const FieldValue & sfbool, double timestamp)
    throw (std::bad_cast)
{
    this->loop = dynamic_cast<const SFBool &>(sfbool);
    this->setModified();
    this->emitEvent("loop_changed", this->loop, timestamp);
}

/**
 * @brief set_pitch eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 */
void AudioClip::processSet_pitch(const FieldValue & sffloat, double timestamp)
    throw (std::bad_cast)
{
    this->pitch = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("pitch_changed", this->pitch, timestamp);
}

/**
 * @brief set_startTime eventIn handler.
 *
 * @param sftime    an SFTime.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sftime is not an SFTime.
 */
void AudioClip::processSet_startTime(const FieldValue & sftime,
                                     const double timestamp)
    throw (std::bad_cast)
{
    this->startTime = dynamic_cast<const SFTime &>(sftime);
    this->setModified();
    this->emitEvent("startTime_changed", this->startTime, timestamp);
}

/**
 * @brief set_stopTime eventIn handler.
 *
 * @param sftime    an SFTime.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sftime is not an SFTime.
 */
void AudioClip::processSet_stopTime(const FieldValue & sftime,
                                    const double timestamp)
    throw (std::bad_cast)
{
    this->stopTime = dynamic_cast<const SFTime &>(sftime);
    this->setModified();
    this->emitEvent("stopTime_changed", this->stopTime, timestamp);
}

/**
 * @brief set_url eventIn handler.
 *
 * @param mfstring  an MFString.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void AudioClip::processSet_url(const FieldValue & mfstring,
                               const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->url = dynamic_cast<const MFString &>(mfstring);
    this->setModified();
    this->emitEvent("url_changed", this->url, timestamp);
}


/**
 * @class BackgroundClass
 *
 * @brief Class object for Background nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
BackgroundClass::BackgroundClass(Browser & browser):
    NodeClass(browser),
    first(0)
{}

/**
 * @brief Destructor.
 */
BackgroundClass::~BackgroundClass() throw ()
{}

/**
 * @brief Set the first Background node in the world.
 *
 * The first Background node in the world is used as the initial background.
 * This method is used by Background::do_initialize.
 *
 * @param background    a Background node.
 */
void BackgroundClass::setFirst(Background & background) throw ()
{
    this->first = &background;
}

/**
 * @brief Check to see if the first node has been set.
 *
 * This method is used by Background::do_initialize.
 *
 * @return @c true if the first node has already been set; @c false otherwise.
 */
bool BackgroundClass::hasFirst() const throw ()
{
    return this->first;
}

/**
 * @brief Push a Background on the top of the bound node stack.
 *
 * @param background    the node to bind.
 * @param timestamp the current time.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void BackgroundClass::bind(Background & background, const double timestamp)
    throw (std::bad_alloc)
{
    using std::find;

    //
    // If the node is already the active node, do nothing.
    //
    if (!this->boundNodes.empty() && &background == this->boundNodes.back()) {
        return;
    }

    //
    // If the node is already on the stack, remove it.
    //
    const BoundNodes::iterator pos =
        find(this->boundNodes.begin(), this->boundNodes.end(), &background);
    if (pos != this->boundNodes.end()) { this->boundNodes.erase(pos); }

    //
    // Send FALSE from the currently active node's isBound.
    //
    if (!this->boundNodes.empty()) {
        Background & current =
                dynamic_cast<Background &>(*this->boundNodes.back());
        current.bound.value = false;
        current.emitEvent("isBound", current.bound, timestamp);
    }

    //
    // Push the node to the top of the stack, and have it send isBound TRUE.
    //
    this->boundNodes.push_back(&background);
    background.bound.value = true;
    background.emitEvent("isBound", background.bound, timestamp);
}

/**
 * @brief Remove a Background from the bound node stack.
 *
 * @param background    the node to unbind.
 * @param timestamp     the current time.
 */
void BackgroundClass::unbind(Background & background, const double timestamp)
    throw ()
{
    using std::find;

    const BoundNodes::iterator pos =
        find(this->boundNodes.begin(), this->boundNodes.end(), &background);
    if (pos != this->boundNodes.end()) {
        background.bound.value = false;
        background.emitEvent("isBound", background.bound, timestamp);

        if (pos == this->boundNodes.end() - 1
                && this->boundNodes.size() > 1) {
            Background & newActive =
                    dynamic_cast<Background &>(**(this->boundNodes.end() - 2));
            newActive.bound.value = true;
            newActive.emitEvent("isBound", newActive.bound, timestamp);
        }
        this->boundNodes.erase(pos);
    }
}

/**
 * @brief NodeClass-specific initialization.
 *
 * @param initialViewpoint  the ViewpointNode that should be bound initially.
 * @param timestamp         the current time.
 */
void BackgroundClass::initialize(ViewpointNode * initialViewpoint,
                                 const double timestamp)
    throw ()
{
    if (this->first) {
        this->first->processEvent("set_bind", SFBool(true), timestamp);
    }
}

namespace {
    /**
     * @brief Load and scale textures as needed.
     */
    Image * getTexture(const MFString & urls, Doc2 & baseDoc,
                       Image * tex, int thisIndex, Viewer & viewer)
    {
        // Check whether the url has already been loaded
        size_t n = urls.value.size();
        if (n > 0) {
            for (int index=thisIndex-1; index >= 0; --index) {
                const char * currentTex = tex[index].url();
                const std::string relPath = baseDoc.urlPath();
                int currentLen = currentTex ? strlen(currentTex) : 0;
                int relPathLen = relPath.length();
                if (relPathLen >= currentLen) { relPathLen = 0; }

                if (currentTex) {
                    for (int i=0; i<n; ++i) {
                        if (urls.value[i] == currentTex
                                || urls.value[i]
                                    == (currentTex + relPathLen)) {
                            return &tex[index];
                        }
                    }
                }
            }

            // Have to load it
            if (!tex[thisIndex].tryURLs(urls, &baseDoc)) {
                std::cerr << "Error: couldn't read Background texture from URL "
                          << urls << std::endl;
            } else if ( tex[thisIndex].pixels() && tex[thisIndex].nc() ) {
                //
                // The texture needs to be scaled.
                //

                // Ensure the image dimensions are powers of two
                int sizes[] = { 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024 };
                const int nSizes = sizeof(sizes) / sizeof(int);
                int w = tex[thisIndex].w();
                int h = tex[thisIndex].h();
                int i, j;
                for (i=0; i<nSizes; ++i) {
                    if (w < sizes[i]) { break; }
                }
                for (j=0; j<nSizes; ++j) {
                    if (h < sizes[j]) { break; }
                }

                if (i > 0 && j > 0) {
                    // Always scale images down in size and reuse the same pixel
                    // memory. This can cause some ugliness...
                    if (w != sizes[i-1] || h != sizes[j-1]) {
                        viewer.scaleTexture(w, h, sizes[i - 1], sizes[j - 1],
                                            tex[thisIndex].nc(),
                                            tex[thisIndex].pixels());
                        tex[thisIndex].setSize(sizes[i - 1], sizes[j - 1]);
                    }
                }
            }
        }

        return &tex[thisIndex];
    }
}

/**
 * @brief NodeClass-specific rendering.
 *
 * Render the active Background node.
 *
 * @param viewer    a Viewer.
 */
void BackgroundClass::render(Viewer & viewer) throw ()
{
    if (!this->boundNodes.empty()) {
        assert(this->boundNodes.back());
        Background & background = *this->boundNodes.back();

        // Background isn't selectable, so don't waste the time.
        if (viewer.getRenderMode() == Viewer::RENDER_MODE_PICK) { return; }

        if (background.viewerObject && background.isModified()) {
            viewer.removeObject(background.viewerObject);
            background.viewerObject = 0;
        }

        if (background.viewerObject) {
            viewer.insertReference(background.viewerObject);
        } else {
            if (background.isModified() || background.texPtr[0] == 0) {
                Doc2 baseDoc(background.getScene()->getURI());
                background.texPtr[0] =
                        getTexture(background.backUrl, baseDoc, background.tex, 0, viewer);
                background.texPtr[1] =
                        getTexture(background.bottomUrl, baseDoc, background.tex, 1, viewer);
                background.texPtr[2] =
                        getTexture(background.frontUrl, baseDoc, background.tex, 2, viewer);
                background.texPtr[3] =
                        getTexture(background.leftUrl, baseDoc, background.tex, 3, viewer);
                background.texPtr[4] =
                        getTexture(background.rightUrl, baseDoc, background.tex, 4, viewer);
                background.texPtr[5] =
                        getTexture(background.topUrl, baseDoc, background.tex, 5, viewer);
            }

            int i, whc[18];    // Width, height, and nComponents for 6 tex
            unsigned char *pixels[6];
            int nPix = 0;

            for (i = 0; i < 6; ++i) {
                whc[3 * i + 0] = background.texPtr[i]->w();
                whc[3 * i + 1] = background.texPtr[i]->h();
                whc[3 * i + 2] = background.texPtr[i]->nc();
                pixels[i] = background.texPtr[i]->pixels();
                if (whc[3 * i + 0] > 0 && whc[3 * i + 1] > 0 && whc[3 * i + 2] > 0
                        && pixels[i]) { ++nPix; }
            }

            background.viewerObject =
                    viewer.insertBackground(background.groundAngle.value,
                                            background.groundColor.value,
                                            background.skyAngle.value,
                                            background.skyColor.value,
                                            whc,
                                            (nPix > 0) ? pixels : 0);

            background.clearModified();
        }
    } else {
        //
        // Default background.
        //
        using std::vector;
        static const vector<float> groundAngle;
        static const vector<color> groundColor;
        static const vector<float> skyAngle;
        static const vector<color> skyColor;
        viewer.insertBackground(groundAngle, groundColor, skyAngle, skyColor);
    }
}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Background nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by BackgroundClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
BackgroundClass::createType(const std::string & id,
                            const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::sfbool, "set_bind"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mffloat, "groundAngle"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfcolor, "groundColor"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "backUrl"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "bottomUrl"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "frontUrl"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "leftUrl"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "rightUrl"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "topUrl"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mffloat, "skyAngle"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfcolor, "skyColor"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isBound")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Background>(*this, id));
    Vrml97NodeTypeImpl<Background> & backgroundNodeType =
            static_cast<Vrml97NodeTypeImpl<Background> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Background>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            backgroundNodeType.addEventIn(supportedInterfaces[0].fieldType,
                                          supportedInterfaces[0].id,
                                          &Background::processSet_bind);
        } else if (*itr == supportedInterfaces[1]) {
            backgroundNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &Background::processSet_groundAngle,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Background, MFFloat>
                                    (&Background::groundAngle)));
        } else if (*itr == supportedInterfaces[2]) {
            backgroundNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &Background::processSet_groundColor,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Background, MFColor>
                                    (&Background::groundColor)));
        } else if (*itr == supportedInterfaces[3]) {
            backgroundNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &Background::processSet_backUrl,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Background, MFString>
                                    (&Background::backUrl)));
        } else if (*itr == supportedInterfaces[4]) {
            backgroundNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &Background::processSet_bottomUrl,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Background, MFString>
                                    (&Background::bottomUrl)));
        } else if (*itr == supportedInterfaces[5]) {
            backgroundNodeType.addExposedField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                &Background::processSet_frontUrl,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Background, MFString>
                                    (&Background::frontUrl)));
        } else if (*itr == supportedInterfaces[6]) {
            backgroundNodeType.addExposedField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                &Background::processSet_leftUrl,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Background, MFString>
                                    (&Background::leftUrl)));
        } else if (*itr == supportedInterfaces[7]) {
            backgroundNodeType.addExposedField(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                &Background::processSet_rightUrl,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Background, MFString>
                                    (&Background::rightUrl)));
        } else if (*itr == supportedInterfaces[8]) {
            backgroundNodeType.addExposedField(
                supportedInterfaces[8].fieldType,
                supportedInterfaces[8].id,
                &Background::processSet_topUrl,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Background, MFString>
                                    (&Background::topUrl)));
        } else if (*itr == supportedInterfaces[9]) {
            backgroundNodeType.addExposedField(
                supportedInterfaces[9].fieldType,
                supportedInterfaces[9].id,
                &Background::processSet_skyAngle,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Background, MFFloat>
                                    (&Background::skyAngle)));
        } else if (*itr == supportedInterfaces[10]) {
            backgroundNodeType.addExposedField(
                supportedInterfaces[10].fieldType,
                supportedInterfaces[10].id,
                &Background::processSet_skyColor,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Background, MFColor>
                                    (&Background::skyColor)));
        } else if (*itr == supportedInterfaces[11]) {
            backgroundNodeType.addEventOut(
                supportedInterfaces[11].fieldType,
                supportedInterfaces[11].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Background, SFBool>
                                    (&Background::bound)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Background
 *
 * @brief Background node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
Background::Background(const NodeType & nodeType,
                       const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope),
    bound(false),
    viewerObject(0)
{
    std::fill(this->texPtr, this->texPtr + 6, static_cast<Image *>(0));
}

/**
 * @brief Destructor.
 */
Background::~Background() throw ()
{
    // remove d_viewerObject...
}

/**
 * @brief Initialize.
 *
 * @param timestamp the current time.
 */
void Background::do_initialize(const double timestamp) throw ()
{
    assert(dynamic_cast<BackgroundClass *>(&this->nodeType.nodeClass));
    BackgroundClass & nodeClass =
            static_cast<BackgroundClass &>(this->nodeType.nodeClass);
    if (!nodeClass.hasFirst()) { nodeClass.setFirst(*this); }
}

/**
 * @brief Shut down.
 *
 * Calls BackgroundClass::unbind to unbind the node if it is bound.
 *
 * @param timestamp the current time.
 */
void Background::do_shutdown(const double timestamp) throw ()
{
    BackgroundClass & nodeClass =
            static_cast<BackgroundClass &>(this->nodeType.nodeClass);
    nodeClass.unbind(*this, timestamp);
}

/**
 * @brief set_bind eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfbool is not an SFBool.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Background::processSet_bind(const FieldValue & sfbool,
                                 const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    const SFBool & bind = dynamic_cast<const SFBool &>(sfbool);
    assert(dynamic_cast<BackgroundClass *>(&this->nodeType.nodeClass));
    BackgroundClass & nodeClass =
            static_cast<BackgroundClass &>(this->nodeType.nodeClass);
    if (bind.value) {
        nodeClass.bind(*this, timestamp);
    } else {
        nodeClass.unbind(*this, timestamp);
    }
}

/**
 * @brief set_groundAngle eventIn handler.
 *
 * @param mffloat   an MFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mffloat is not an MFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Background::processSet_groundAngle(const FieldValue & mffloat,
                                        const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->groundAngle = dynamic_cast<const MFFloat &>(mffloat);
    this->setModified();
    this->emitEvent("groundAngle_changed", this->groundAngle, timestamp);
}

/**
 * @brief set_groundColor eventIn handler.
 *
 * @param mfcolor   an MFColor.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfcolor is not an MFColor.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Background::processSet_groundColor(const FieldValue & mfcolor,
                                        const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->groundColor = dynamic_cast<const MFColor &>(mfcolor);
    this->setModified();
    this->emitEvent("groundColor_changed", this->groundColor, timestamp);
}

/**
 * @brief set_backUrl eventIn handler.
 *
 * @param mfstring  an MFString.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Background::processSet_backUrl(const FieldValue & mfstring,
                                    const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->backUrl = dynamic_cast<const MFString &>(mfstring);
    this->setModified();
    this->emitEvent("backUrl_changed", this->backUrl, timestamp);
}

/**
 * @brief set_bottomUrl eventIn handler.
 *
 * @param mfstring  an MFString.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Background::processSet_bottomUrl(const FieldValue & mfstring,
                                      const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->bottomUrl = dynamic_cast<const MFString &>(mfstring);
    this->setModified();
    this->emitEvent("bottomUrl_changed", this->bottomUrl, timestamp);
}

/**
 * @brief set_frontUrl eventIn handler.
 *
 * @param mfstring  an MFString.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Background::processSet_frontUrl(const FieldValue & mfstring,
                                     const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->frontUrl = dynamic_cast<const MFString &>(mfstring);
    this->setModified();
    this->emitEvent("frontUrl_changed", this->backUrl, timestamp);
}

/**
 * @brief set_leftUrl eventIn handler.
 *
 * @param mfstring  an MFString.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Background::processSet_leftUrl(const FieldValue & mfstring,
                                    const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->leftUrl = dynamic_cast<const MFString &>(mfstring);
    this->setModified();
    this->emitEvent("leftUrl_changed", this->leftUrl, timestamp);
}

/**
 * @brief set_rightUrl eventIn handler.
 *
 * @param mfstring  an MFString.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Background::processSet_rightUrl(const FieldValue & mfstring,
                                     const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->rightUrl = dynamic_cast<const MFString &>(mfstring);
    this->setModified();
    this->emitEvent("rightUrl_changed", this->rightUrl, timestamp);
}

/**
 * @brief set_topUrl eventIn handler.
 *
 * @param mfstring  an MFString.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Background::processSet_topUrl(const FieldValue & mfstring,
                                   const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->topUrl = dynamic_cast<const MFString &>(mfstring);
    this->setModified();
    this->emitEvent("topUrl_changed", this->topUrl, timestamp);
}

/**
 * @brief set_skyAngle eventIn handler.
 *
 * @param mffloat   an MFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mffloat is not an MFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Background::processSet_skyAngle(const FieldValue & mffloat,
                                     const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->skyAngle = dynamic_cast<const MFFloat &>(mffloat);
    this->setModified();
    this->emitEvent("skyAngle_changed", this->skyAngle, timestamp);
}

/**
 * @brief set_skyColor eventIn handler.
 *
 * @param mfcolor   an MFColor.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfcolor is not an MFColor.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Background::processSet_skyColor(const FieldValue & mfcolor,
                                     const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->skyColor = dynamic_cast<const MFColor &>(mfcolor);
    this->setModified();
    this->emitEvent("skyColor_changed", this->skyColor, timestamp);
}


/**
 * @class BillboardClass
 *
 * @brief Class object for Billboard nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
BillboardClass::BillboardClass(Browser & browser): NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
BillboardClass::~BillboardClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Billboard nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by BillboardClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
BillboardClass::createType(const std::string & id,
                           const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::mfnode, "addChildren"),
        NodeInterface(NodeInterface::eventIn, FieldValue::mfnode, "removeChildren"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "axisOfRotation"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfnode, "children"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "bboxCenter"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "bboxSize")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Billboard>(*this, id));
    Vrml97NodeTypeImpl<Billboard> & billboardNodeType =
            static_cast<Vrml97NodeTypeImpl<Billboard> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Billboard>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            billboardNodeType.addEventIn(supportedInterfaces[0].fieldType,
                                         supportedInterfaces[0].id,
                                         &Billboard::processAddChildren);
        } else if (*itr == supportedInterfaces[1]) {
            billboardNodeType.addEventIn(supportedInterfaces[1].fieldType,
                                         supportedInterfaces[1].id,
                                         &Billboard::processRemoveChildren);
        } else if (*itr == supportedInterfaces[2]) {
            billboardNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &Billboard::processSet_axisOfRotation,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Billboard, SFVec3f>
                                    (&Billboard::axisOfRotation)));
        } else if (*itr == supportedInterfaces[3]) {
            billboardNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &Billboard::processSet_children,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Billboard, MFNode>
                                    (&Billboard::children)));
        } else if (*itr == supportedInterfaces[4]) {
            billboardNodeType.addField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Billboard, SFVec3f>
                                    (&Billboard::bboxCenter)));
        } else if (*itr == supportedInterfaces[5]) {
            billboardNodeType.addField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Billboard, SFVec3f>
                                    (&Billboard::bboxSize)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Billboard
 *
 * @brief Billboard node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
Billboard::Billboard(const NodeType & nodeType,
                     const ScopePtr & scope):
    Node(nodeType, scope),
    ChildNode(nodeType, scope),
    GroupingNode(nodeType, scope),
    Group(nodeType, scope),
    axisOfRotation(vec3f(0.0, 1.0, 0.0)),
    xformObject(0)
{}

/**
 * @brief Destructor.
 */
Billboard::~Billboard() throw ()
{
    // delete xformObject...
}

/**
 * @brief Render the node.
 *
 * @param viewer    a Viewer.
 * @param context   the rendering context.
 */
void Billboard::render(Viewer & viewer, VrmlRenderContext context)
{
    mat4f LM;
    mat4f new_LM = context.getMatrix();
    billboard_to_matrix(this, new_LM, LM);
    new_LM = LM * new_LM;
    context.setMatrix(new_LM);

    if (this->xformObject && this->isModified()) {
        viewer.removeObject(this->xformObject);
        this->xformObject = 0;
    }

    if (this->xformObject) {
        viewer.insertReference(this->xformObject);
    } else if (this->children.value.size() > 0) {
        this->xformObject = viewer.beginObject(this->getId().c_str());

        viewer.transform(LM);

        // Render children
        this->Group::render(viewer, context);

        viewer.endObject();
    }

    this->clearModified();
}

/**
 * @brief Calculate bb transformation matrix and store it in @p M.
 *
 * Here we are dealing with mat4f format (Matrices are stored
 * in row-major order).
 *
 * @param t_arg a pointer to a Billboard node.
 * @param L_MV  input ModelView transformation matrix.
 * @retval M    a copy of the resulting transform.
 */
void Billboard::billboard_to_matrix(const Billboard* t_arg,
                                    const mat4f & L_MV,
                                    mat4f & M)
{
    mat4f MV = L_MV.inverse();

    // Viewer position in local coordinate system
    vec3f VP(MV[3][0], MV[3][1], MV[3][2]);
    vec3f NVP = VP.normalize();

    // Viewer-alignment
    if ((t_arg->axisOfRotation.value[0] == 0)
            && (t_arg->axisOfRotation.value[1] == 0)
            && (t_arg->axisOfRotation.value[2] == 0)) {
        //
        // Viewer's up vector
        //
        vec3f Y(MV[1][0], MV[1][1], MV[1][2]);
        vec3f NY = Y.normalize();

        // get x-vector from the cross product of Viewer's
        // up vector and billboard-to-viewer vector.
        vec3f X = NY * NVP;
        M[0][0] = X[0]; M[0][1] = X[1]; M[0][2] = X[2]; M[0][3] = 0.0;
        M[1][0] = NY[0]; M[1][1] = NY[1]; M[1][2] = NY[2]; M[1][3] = 0.0;
        M[2][0] = NVP[0]; M[2][1] = NVP[1]; M[2][2] = NVP[2]; M[2][3] = 0.0,
        M[3][0] = M[3][1] = M[3][2] = 0.0; M[3][3] = 1.0;
    } else { // use axis of rotation
        // axis of rotation will be the y-axis vector
        vec3f Y(t_arg->axisOfRotation.value);

        // Plane defined by the axisOfRotation and billboard-to-viewer vector
        vec3f X = (Y * VP).normalize();

        // Get Z axis vector from cross product of X and Y
        vec3f Z = X * Y;

        // Transform Z axis vector of current coordinate system to new
        // coordinate system.
        float nz[3];
        nz[0] = X[2]; nz[1] = Y[2]; nz[2] = Z[2];

        // calculate the angle by which the Z axis vector of current coordinate
        // system has to be rotated around the Y axis to new coordinate system.
        float angle = acos(nz[2]);
        if(nz[0] > 0) { angle = -angle; }
        M = mat4f::rotation(rotation(Y, angle));
    }
}

/**
 * @brief set_axisOfRotation eventIn handler.
 *
 * @param sfvec3f   an SFVec3f.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void Billboard::processSet_axisOfRotation(const FieldValue & sfvec3f,
                                          const double timestamp)
    throw (std::bad_cast)
{
    this->axisOfRotation = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->emitEvent("axisOfRotation_changed", this->axisOfRotation, timestamp);
}


/**
 * @class BoxClass
 *
 * @brief Class object for Box nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
BoxClass::BoxClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
BoxClass::~BoxClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Box nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by BoxClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr BoxClass::createType(const std::string & id,
                                       const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterface =
            NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "size");
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Box>(*this, id));
    Vrml97NodeTypeImpl<Box> & boxNodeType =
            static_cast<Vrml97NodeTypeImpl<Box> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Box>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterface) {
            boxNodeType.addField(
                supportedInterface.fieldType,
                supportedInterface.id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Box, SFVec3f>(&Box::size)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Box
 *
 * @brief Box node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
Box::Box(const NodeType & nodeType,
         const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractGeometry(nodeType, scope),
    size(vec3f(2.0, 2.0, 2.0))
{
    this->setBVolumeDirty(true); // lazy calc of bvolume
}

/**
 * @brief Destructor.
 */
Box::~Box() throw ()
{}

/**
 * @brief Insert this geometry into @p viewer's display list.
 *
 * @param viewer    a Viewer.
 * @param context   the rendering context.
 *
 * @return display object identifier.
 */
Viewer::Object Box::insertGeometry(Viewer & viewer,
                                   const VrmlRenderContext context)
{
    return viewer.insertBox(this->size.value);
}

/**
 * @brief Get the bounding volume.
 *
 * @return the bounding volume associated with the node.
 */
const BVolume * Box::getBVolume() const
{
    using OpenVRML_::length;

    if (this->isBVolumeDirty()) {
        const float corner[3] = { this->size.value.x() / 2.0f,
                                  this->size.value.y() / 2.0f,
                                  this->size.value.z() / 2.0f };
        float r = length(corner);
        ((Box*)this)->bsphere.setRadius(r);
        ((Box*)this)->setBVolumeDirty(false); // logical const
    }
    return &this->bsphere;
}


/**
 * @class CollisionClass
 *
 * @brief Class object for Collision nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
CollisionClass::CollisionClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
CollisionClass::~CollisionClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Collision nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by CollisionClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        CollisionClass::createType(const std::string & id,
                                   const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::mfnode, "addChildren"),
        NodeInterface(NodeInterface::eventIn, FieldValue::mfnode, "removeChildren"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfnode, "children"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "collide"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "bboxCenter"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "bboxSize"),
        NodeInterface(NodeInterface::field, FieldValue::sfnode, "proxy"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sftime, "collideTime")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Collision>(*this, id));
    Vrml97NodeTypeImpl<Collision> & collisionNodeType =
            static_cast<Vrml97NodeTypeImpl<Collision> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Collision>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            collisionNodeType.addEventIn(supportedInterfaces[0].fieldType,
                                         supportedInterfaces[0].id,
                                         &Collision::processAddChildren);
        } else if (*itr == supportedInterfaces[1]) {
            collisionNodeType.addEventIn(supportedInterfaces[1].fieldType,
                                         supportedInterfaces[1].id,
                                         &Collision::processRemoveChildren);
        } else if (*itr == supportedInterfaces[2]) {
            collisionNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &Collision::processSet_children,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Collision, MFNode>
                                    (&Collision::children)));
        } else if (*itr == supportedInterfaces[3]) {
            collisionNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &Collision::processSet_collide,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Collision, SFBool>
                                    (&Collision::collide)));
        } else if (*itr == supportedInterfaces[4]) {
            collisionNodeType.addField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Collision, SFVec3f>
                                    (&Collision::bboxCenter)));
        } else if (*itr == supportedInterfaces[5]) {
            collisionNodeType.addField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Collision, SFVec3f>
                                    (&Collision::bboxSize)));
        } else if (*itr == supportedInterfaces[6]) {
            collisionNodeType.addField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Collision, SFNode>
                                    (&Collision::proxy)));
        } else if (*itr == supportedInterfaces[7]) {
            collisionNodeType.addEventOut(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Collision, SFTime>
                                    (&Collision::collideTime)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Collision
 *
 * @brief Collision node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
Collision::Collision(const NodeType & nodeType,
                     const ScopePtr & scope):
    Node(nodeType, scope),
    ChildNode(nodeType, scope),
    GroupingNode(nodeType, scope),
    Group(nodeType, scope),
    collide(true)
{}

/**
 * @brief Destructor.
 */
Collision::~Collision() throw () {}

/**
 * @brief Determine whether the node has been modified.
 *
 * @return @c true if the node or one of its children has been modified,
 *      @c false otherwise.
 */
bool Collision::isModified() const {
  return ((this->proxy.value && this->proxy.value->isModified())
          || this->Group::isModified());
}

/**
 * @brief set_collide eventIn handler.
 *
 * @param sfbool    an SFBool.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void Collision::processSet_collide(const FieldValue & sfbool,
                                   const double timestamp)
        throw (std::bad_cast) {
    this->collide = dynamic_cast<const SFBool &>(sfbool);
    this->emitEvent("collide_changed", this->collide, timestamp);
}


/**
 * @class ColorClass
 *
 * @brief Class object for Color nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this node class object.
 */
ColorClass::ColorClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
ColorClass::~ColorClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Color nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by ColorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr ColorClass::createType(const std::string & id,
                                         const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterface =
            NodeInterface(NodeInterface::exposedField, FieldValue::mfcolor, "color");
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Color>(*this, id));
    Vrml97NodeTypeImpl<Color> & colorNodeType =
            static_cast<Vrml97NodeTypeImpl<Color> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Color>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterface) {
            colorNodeType.addExposedField(
                supportedInterface.fieldType,
                supportedInterface.id,
                &Color::processSet_color,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Color, MFColor>
                                    (&Color::color)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Color
 *
 * @brief Color node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with this node.
 * @param scope     the Scope to which the node belongs.
 */
Color::Color(const NodeType & nodeType,
             const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractBase(nodeType, scope),
        ColorNode(nodeType, scope) {}

/**
 * @brief Destructor.
 */
Color::~Color() throw () {}

/**
 * @brief Get the color array.
 *
 * @return the color array associated with the node.
 */
const std::vector<color> & Color::getColor() const throw ()
{
    return this->color.value;
}

/**
 * @brief set_color eventIn handler.
 *
 * @param mfcolor   an MFColor.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfcolor is not an MFColor.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Color::processSet_color(const FieldValue & mfcolor, const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->color = dynamic_cast<const MFColor &>(mfcolor);
    this->setModified();
    this->emitEvent("color_changed", this->color, timestamp);
}


/**
 * @class ColorInterpolatorClass
 *
 * @brief Class object for ColorInterpolator nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
ColorInterpolatorClass::ColorInterpolatorClass(Browser & browser):
        NodeClass(browser) {}

/**
 * @brief Destructor.
 */
ColorInterpolatorClass::~ColorInterpolatorClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating ColorInterpolator
 *      nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by ColorInterpolatorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr ColorInterpolatorClass::
        createType(const std::string & id,
                   const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::sffloat, "set_fraction"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mffloat, "key"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfcolor, "keyValue"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfcolor, "value_changed")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<ColorInterpolator>(*this, id));
    Vrml97NodeTypeImpl<ColorInterpolator> & colorInterpolatorNodeType =
            static_cast<Vrml97NodeTypeImpl<ColorInterpolator> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<ColorInterpolator>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            colorInterpolatorNodeType
                    .addEventIn(supportedInterfaces[0].fieldType,
                                supportedInterfaces[0].id,
                                &ColorInterpolator::processSet_fraction);
        } else if (*itr == supportedInterfaces[1]) {
            colorInterpolatorNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &ColorInterpolator::processSet_key,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ColorInterpolator, MFFloat>
                                    (&ColorInterpolator::key)));
        } else if (*itr == supportedInterfaces[2]) {
            colorInterpolatorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &ColorInterpolator::processSet_keyValue,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ColorInterpolator, MFColor>
                                    (&ColorInterpolator::keyValue)));
        } else if (*itr == supportedInterfaces[3]) {
            colorInterpolatorNodeType.addEventOut(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ColorInterpolator, SFColor>
                                    (&ColorInterpolator::value)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class ColorInterpolator
 *
 * @brief ColorInterpolator node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
ColorInterpolator::ColorInterpolator(const NodeType & nodeType,
                                     const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractChild(nodeType, scope) {}

/**
 * @brief Destructor.
 */
ColorInterpolator::~ColorInterpolator() throw () {}

/**
 * @brief set_fraction eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void ColorInterpolator::processSet_fraction(const FieldValue & sffloat,
                                            const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    float f = dynamic_cast<const SFFloat &>(sffloat).value;

    int n = this->key.value.size() - 1;
    if (f < this->key.value[0]) {
        this->value.value = this->keyValue.value[0];
    } else if (f > this->key.value[n]) {
        this->value.value = this->keyValue.value[n];
    } else {
        // convert to HSV for the interpolation...
        for (int i = 0; i < n; ++i) {
            if (this->key.value[i] <= f
                    && f <= this->key.value[i + 1]) {
                const color & rgb1 = this->keyValue.value[i];
                const color & rgb2 = this->keyValue.value[i + 1];

                f = (f - this->key.value[i])
                    / (this->key.value[i + 1] - this->key.value[i]);
                float hsv1[3], hsv2[3];
                rgb1.hsv(hsv1);
                rgb2.hsv(hsv2);

                // Interpolate angles via the shortest direction
                if (fabs(hsv2[0] - hsv1[0]) > 180.0) {
                    if (hsv2[0] > hsv1[0]) {
                        hsv1[0] += 360.0;
                    } else {
                        hsv2[0] += 360.0;
                    }
                }
                float h = hsv1[0] + f * (hsv2[0] - hsv1[0]);
                float s = hsv1[1] + f * (hsv2[1] - hsv1[1]);
                float v = hsv1[2] + f * (hsv2[2] - hsv1[2]);
                if (h >= 360.0) {
                    h -= 360.0;
                } else if (h < 0.0) {
                    h += 360.0;
                }
                this->value.value.hsv(h, s, v);
                break;
            }
        }
    }

    // Send the new value
    this->emitEvent("value_changed", this->value, timestamp);
}

/**
 * @brief set_key eventIn handler.
 *
 * @param mffloat   an MFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mffloat is not an MFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void ColorInterpolator::processSet_key(const FieldValue & mffloat,
                                       const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->key = dynamic_cast<const MFFloat &>(mffloat);
    this->emitEvent("key_changed", this->key, timestamp);
}

/**
 * @brief set_keyValue eventIn handler.
 *
 * @param mfcolor   an MFColor.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfcolor is not an MFColor.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void ColorInterpolator::processSet_keyValue(const FieldValue & mfcolor,
                                            const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->keyValue = dynamic_cast<const MFColor &>(mfcolor);
    this->emitEvent("keyValue_changed", this->keyValue, timestamp);
}


/**
 * @class ConeClass
 *
 * @brief Class object for Cone nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
ConeClass::ConeClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
ConeClass::~ConeClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Cone nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by ConeClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr ConeClass::createType(const std::string & id,
                                        const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::field, FieldValue::sffloat, "bottomRadius"),
        NodeInterface(NodeInterface::field, FieldValue::sffloat, "height"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "side"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "bottom")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Cone>(*this, id));
    Vrml97NodeTypeImpl<Cone> & coneNodeType =
            static_cast<Vrml97NodeTypeImpl<Cone> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Cone>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            coneNodeType.addField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Cone, SFFloat>
                                    (&Cone::bottomRadius)));
        } else if (*itr == supportedInterfaces[1]) {
            coneNodeType.addField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Cone, SFFloat>
                                    (&Cone::height)));
        } else if (*itr == supportedInterfaces[2]) {
            coneNodeType.addField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Cone, SFBool>
                                    (&Cone::side)));
        } else if (*itr == supportedInterfaces[3]) {
            coneNodeType.addField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Cone, SFBool>
                                    (&Cone::bottom)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Cone
 *
 * @brief Cone node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
Cone::Cone(const NodeType & nodeType,
           const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractGeometry(nodeType, scope),
        bottom(true),
        bottomRadius(1.0),
        height(2.0),
        side(true) {}

/**
 * @brief Destructor.
 */
Cone::~Cone() throw () {}

/**
 * @brief Insert this geometry into @p viewer's display list.
 *
 * @param viewer    a Viewer.
 * @param context   the rendering context.
 */
Viewer::Object Cone::insertGeometry(Viewer & viewer,
                                    const VrmlRenderContext context)
{
    return viewer.insertCone(this->height.value,
                             this->bottomRadius.value,
                             this->bottom.value,
                             this->side.value);
}


/**
 * @class CoordinateClass
 *
 * @brief Class object for Material nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this node class object.
 */
CoordinateClass::CoordinateClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
CoordinateClass::~CoordinateClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Coordinate nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by CoordinateClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        CoordinateClass::createType(const std::string & id,
                                    const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterface =
            NodeInterface(NodeInterface::exposedField, FieldValue::mfvec3f, "point");
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Coordinate>(*this, id));
    Vrml97NodeTypeImpl<Coordinate> & coordinateNodeType =
            static_cast<Vrml97NodeTypeImpl<Coordinate> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Coordinate>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterface) {
            coordinateNodeType.addExposedField(
                supportedInterface.fieldType,
                supportedInterface.id,
                &Coordinate::processSet_point,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Coordinate, MFVec3f>
                                    (&Coordinate::point)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Coordinate
 *
 * @brief Coordinate node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node.
 * @param scope     the Scope to which the node belongs.
 */
Coordinate::Coordinate(const NodeType & nodeType,
                       const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractBase(nodeType, scope),
        CoordinateNode(nodeType, scope) {}

/**
 * @brief Destructor.
 */
Coordinate::~Coordinate() throw () {}

/**
 * @brief set_point eventIn handler.
 *
 * @param mfvec3f   an array of vectors representing points.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfvec3f is not an MFVec3f.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Coordinate::processSet_point(const FieldValue & mfvec3f,
                                  const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->point = dynamic_cast<const MFVec3f &>(mfvec3f);
    this->setModified();
    this->emitEvent("point_changed", this->point, timestamp);
}

/**
 * @brief Get the points encapsulated by this node.
 *
 * @return the array of points for this node.
 */
const std::vector<vec3f> & Coordinate::getPoint() const throw ()
{
    return this->point.value;
}


/**
 * @class CoordinateInterpolatorClass
 *
 * @brief Class object for CoordinateInterpolator nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
CoordinateInterpolatorClass::CoordinateInterpolatorClass(Browser & browser):
        NodeClass(browser) {}

/**
 * @brief Destructor.
 */
CoordinateInterpolatorClass::~CoordinateInterpolatorClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating
 *      CoordinateInterpolator nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by CoordinateInterpolatorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr CoordinateInterpolatorClass::
        createType(const std::string & id,
                   const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::sffloat, "set_fraction"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mffloat, "key"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfvec3f, "keyValue"),
        NodeInterface(NodeInterface::eventOut, FieldValue::mfvec3f, "value_changed")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<CoordinateInterpolator>(*this, id));
    Vrml97NodeTypeImpl<CoordinateInterpolator> & coordinateInterpolatorNodeType =
            static_cast<Vrml97NodeTypeImpl<CoordinateInterpolator> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<CoordinateInterpolator>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            coordinateInterpolatorNodeType
                    .addEventIn(supportedInterfaces[0].fieldType,
                                supportedInterfaces[0].id,
                                &CoordinateInterpolator::processSet_fraction);
        } else if (*itr == supportedInterfaces[1]) {
            coordinateInterpolatorNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &CoordinateInterpolator::processSet_key,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<CoordinateInterpolator, MFFloat>
                                    (&CoordinateInterpolator::key)));
        } else if (*itr == supportedInterfaces[2]) {
            coordinateInterpolatorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &CoordinateInterpolator::processSet_keyValue,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<CoordinateInterpolator, MFVec3f>
                                    (&CoordinateInterpolator::keyValue)));
        } else if (*itr == supportedInterfaces[3]) {
            coordinateInterpolatorNodeType.addEventOut(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<CoordinateInterpolator, MFVec3f>
                                    (&CoordinateInterpolator::value)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class CoordinateInterpolator
 *
 * @brief CoordinateInterpolator node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
CoordinateInterpolator::CoordinateInterpolator(const NodeType & nodeType,
                                               const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractChild(nodeType, scope) {}

/**
 * @brief Destructor.
 */
CoordinateInterpolator::~CoordinateInterpolator() throw () {}

/**
 * @brief set_fraction eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void CoordinateInterpolator::processSet_fraction(const FieldValue & sffloat,
                                                 const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    float f = dynamic_cast<const SFFloat &>(sffloat).value;

    size_t nCoords = this->keyValue.value.size() / this->key.value.size();
    size_t n = this->key.value.size() - 1;

    if (f < this->key.value[0]) {
        this->value.value.assign(this->keyValue.value.begin(),
                                 this->keyValue.value.begin() + nCoords);
    } else if (f > this->key.value[n]) {
        this->value.value
                .assign(this->keyValue.value.begin() + n * nCoords,
                        this->keyValue.value.begin() + (n + 1) * nCoords);
    } else {
        // Reserve enough space for the new value
        this->value.value.resize(nCoords);

        for (size_t i = 0; i < n; ++i) {
            if (this->key.value[i] <= f
                    && f <= this->key.value[i + 1]) {
                std::vector<vec3f>::const_iterator v1 =
                        this->keyValue.value.begin() + i * nCoords;
                std::vector<vec3f>::const_iterator v2 =
                        this->keyValue.value.begin() + (i + 1) * nCoords;

                f = (f - this->key.value[i])
                    / (this->key.value[i + 1] - this->key.value[i]);

                for (size_t j = 0; j < nCoords; ++j) {
                    const vec3f vec(v1->x() + f * (v2->x() - v1->x()),
                                    v1->y() + f * (v2->y() - v1->y()),
                                    v1->z() + f * (v2->z() - v1->z()));
                    this->value.value[j] = vec;
                    ++v1;
                    ++v2;
                }
                break;
            }
        }
    }

    // Send the new value
    this->emitEvent("value_changed", this->value, timestamp);
}

/**
 * @brief set_key eventIn handler.
 *
 * @param mffloat   an MFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mffloat is not an MFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void CoordinateInterpolator::processSet_key(const FieldValue & mffloat,
                                            const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->key = dynamic_cast<const MFFloat &>(mffloat);
    this->emitEvent("key_changed", this->key, timestamp);
}

/**
 * @brief set_keyValue field mutator.
 *
 * @param mfvec3f   an MFVec3f.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfvec3f is not an MFVec3f.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void CoordinateInterpolator::processSet_keyValue(const FieldValue & mfvec3f,
                                                 const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->keyValue = dynamic_cast<const MFVec3f &>(mfvec3f);
    this->emitEvent("keyValue_changed", this->keyValue, timestamp);
}


/**
 * @class CylinderClass
 *
 * @brief Class object for Cylinder nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
CylinderClass::CylinderClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
CylinderClass::~CylinderClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Cylinder nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by CylinderClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr CylinderClass::createType(const std::string & id,
                                            const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "bottom"),
        NodeInterface(NodeInterface::field, FieldValue::sffloat, "height"),
        NodeInterface(NodeInterface::field, FieldValue::sffloat, "radius"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "side"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "top")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Cylinder>(*this, id));
    Vrml97NodeTypeImpl<Cylinder> & cylinderNodeType =
            static_cast<Vrml97NodeTypeImpl<Cylinder> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Cylinder>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            cylinderNodeType.addField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Cylinder, SFBool>
                                    (&Cylinder::bottom)));
        } else if (*itr == supportedInterfaces[1]) {
            cylinderNodeType.addField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Cylinder, SFFloat>
                                    (&Cylinder::height)));
        } else if (*itr == supportedInterfaces[2]) {
            cylinderNodeType.addField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Cylinder, SFFloat>
                                    (&Cylinder::radius)));
        } else if (*itr == supportedInterfaces[3]) {
            cylinderNodeType.addField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Cylinder, SFBool>
                                    (&Cylinder::side)));
        } else if (*itr == supportedInterfaces[4]) {
            cylinderNodeType.addField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Cylinder, SFBool>
                                    (&Cylinder::top)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Cylinder
 *
 * @brief Cylinder node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
Cylinder::Cylinder(const NodeType & nodeType,
                   const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractGeometry(nodeType, scope),
        bottom(true),
        height(2.0),
        radius(1.0),
        side(true),
        top(true) {}

/**
 * @brief Destructor.
 */
Cylinder::~Cylinder() throw () {
    // need access to viewer to remove d_viewerObject...
}

/**
 * @brief Insert this geometry into @p viewer's display list.
 *
 * @param viewer    a Viewer.
 * @param context   the rendering context.
 */
Viewer::Object Cylinder::insertGeometry(Viewer & viewer,
                                        const VrmlRenderContext context)
{
    return viewer.insertCylinder(this->height.value,
                                 this->radius.value,
                                 this->bottom.value,
                                 this->side.value,
                                 this->top.value);
}


/**
 * @class CylinderSensorClass
 *
 * @brief Class object for CylinderSensor nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
CylinderSensorClass::CylinderSensorClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
CylinderSensorClass::~CylinderSensorClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating CylinderSensor nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by CylinderSensorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        CylinderSensorClass::createType(const std::string & id,
                                        const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "autoOffset"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "diskAngle"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "enabled"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "maxAngle"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "minAngle"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "offset"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isActive"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfrotation, "rotation_changed"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfvec3f, "trackPoint_changed")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<CylinderSensor>(*this, id));
    Vrml97NodeTypeImpl<CylinderSensor> & cylinderSensorNodeType =
            static_cast<Vrml97NodeTypeImpl<CylinderSensor> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<CylinderSensor>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            cylinderSensorNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &CylinderSensor::processSet_autoOffset,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<CylinderSensor, SFBool>
                                    (&CylinderSensor::autoOffset)));
        } else if (*itr == supportedInterfaces[1]) {
            cylinderSensorNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &CylinderSensor::processSet_diskAngle,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<CylinderSensor, SFFloat>
                                    (&CylinderSensor::diskAngle)));
        } else if (*itr == supportedInterfaces[2]) {
            cylinderSensorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &CylinderSensor::processSet_enabled,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<CylinderSensor, SFBool>
                                    (&CylinderSensor::enabled)));
        } else if (*itr == supportedInterfaces[3]) {
            cylinderSensorNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &CylinderSensor::processSet_maxAngle,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<CylinderSensor, SFFloat>
                                    (&CylinderSensor::maxAngle)));
        } else if (*itr == supportedInterfaces[4]) {
            cylinderSensorNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &CylinderSensor::processSet_minAngle,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<CylinderSensor, SFFloat>
                                    (&CylinderSensor::minAngle)));
        } else if (*itr == supportedInterfaces[5]) {
            cylinderSensorNodeType.addExposedField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                &CylinderSensor::processSet_offset,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<CylinderSensor, SFFloat>
                                    (&CylinderSensor::offset)));
        } else if (*itr == supportedInterfaces[6]) {
            cylinderSensorNodeType.addEventOut(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<CylinderSensor, SFBool>
                                    (&CylinderSensor::active)));
        } else if (*itr == supportedInterfaces[7]) {
            cylinderSensorNodeType.addEventOut(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<CylinderSensor, SFRotation>
                                    (&CylinderSensor::rotation)));
        } else if (*itr == supportedInterfaces[8]) {
            cylinderSensorNodeType.addEventOut(
                supportedInterfaces[8].fieldType,
                supportedInterfaces[8].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<CylinderSensor, SFVec3f>
                                    (&CylinderSensor::trackPoint)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class CylinderSensor
 *
 * @brief CylinderSensor node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
CylinderSensor::CylinderSensor(const NodeType & nodeType,
                               const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractChild(nodeType, scope),
        autoOffset(true),
        diskAngle(0.262),
        enabled(true),
        maxAngle(-1.0),
        minAngle(0.0),
        offset(0.0),
        active(false) {
    this->setModified();
}

/**
 * @brief Destructor.
 */
CylinderSensor::~CylinderSensor() throw () {}

/**
 * @brief Cast to a CylinderSensor.
 */
CylinderSensor* CylinderSensor::toCylinderSensor() const {   // mgiger 6/16/00
    return (CylinderSensor*) this;
}

/**
 * @brief Render the node.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void CylinderSensor::render(Viewer & viewer, VrmlRenderContext context)
{
    //
    // Store the ModelView matrix which is calculated at the time of rendering
    // in render-context. This matrix will be in use at the time of activation.
    //
    this->modelview = context.getMatrix();
}

void CylinderSensor::activate(double timeStamp, bool isActive, double *p)
{
    using OpenVRML_::fpequal;

    // Become active
    if (isActive && !this->active.value) {
        this->active.value = isActive;

        // set activation point in local coords
        vec3f Vec(p[0], p[1], p[2]);
        this->activationMatrix = this->modelview.inverse();
        Vec = Vec * this->activationMatrix;
        this->activationPoint.value = Vec;
        // Bearing vector in local coordinate system
        Vec[0] = this->activationMatrix[2][0];
        Vec[1] = this->activationMatrix[2][1];
        Vec[2] = this->activationMatrix[2][2];
        vec3f BV(Vec);
        vec3f Y(0,1,0);
        BV = BV.normalize();
        double ang = acos(BV.dot(Y));
        if (ang > pi_2) { ang = pi - ang; }
        if (ang < this->diskAngle.value) {
            disk.value = true;
        } else {
            disk.value = false;
        }
        // send message
        this->emitEvent("isActive", this->active, timeStamp);
    }

    // Become inactive
    else if (!isActive && this->active.value) {
        this->active.value = isActive;
        this->emitEvent("isActive", this->active, timeStamp);

        // save auto offset of rotation
        if (this->autoOffset.value) {
            this->offset = rotation_val;
            this->emitEvent("offset_changed", this->offset, timeStamp);
        }
    }

        // Tracking
    else if (isActive) {
        // get local coord for touch point
        vec3f Vec(p[0], p[1], p[2]);
        Vec = Vec * this->activationMatrix;
        this->trackPoint.value = Vec;
        this->emitEvent("trackPoint_changed", this->trackPoint, timeStamp);
        vec3f tempv;
        float rot, radius;
        vec3f dir1(Vec[0], 0, Vec[2]);
        if (disk.value) {
            radius = 1.0;
        } else {
            radius = dir1.length();    // get the radius
        }
        dir1 = dir1.normalize();
        vec3f dir2(this->activationPoint.value.x(),
                   0,
                   this->activationPoint.value.z());
        dir2 = dir2.normalize();
        tempv = dir2 * dir1;
        vec3f cx(tempv);
        cx = cx.normalize();
        if (cx.length() == 0.0) { return; }
        rot = radius * acos(dir2.dot(dir1));
        if (fpequal(cx.y(), -1.0)) { rot = -rot; }
        if (this->autoOffset.value) {
            rot = this->offset.value + rot;
        }
        if (this->minAngle.value < this->maxAngle.value) {
            if (rot < this->minAngle.value) {
                rot = this->minAngle.value;
            } else if (rot > this->maxAngle.value) {
                rot = this->maxAngle.value;
            }
        }
        rotation_val.value = rot;
        this->rotation.value = OpenVRML::rotation(0, 1, 0, rot);

        this->emitEvent("rotation_changed", this->rotation, timeStamp);
    }
}

/**
 * @brief set_autoOffset eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void CylinderSensor::processSet_autoOffset(const FieldValue & sfbool,
                                           const double timestamp)
        throw (std::bad_cast) {
    this->autoOffset = dynamic_cast<const SFBool &>(sfbool);
    this->emitEvent("autoOffset_changed", this->autoOffset, timestamp);
}

/**
 * @brief set_diskAngle eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void CylinderSensor::processSet_diskAngle(const FieldValue & sffloat,
                                          const double timestamp)
        throw (std::bad_cast) {
    this->diskAngle = dynamic_cast<const SFFloat &>(sffloat);
    this->emitEvent("diskAngle_changed", this->diskAngle, timestamp);
}

/**
 * @brief set_enabled eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void CylinderSensor::processSet_enabled(const FieldValue & sfbool,
                                        const double timestamp)
        throw (std::bad_cast) {
    this->enabled = dynamic_cast<const SFBool &>(sfbool);
    this->emitEvent("enabled_changed", this->enabled, timestamp);
}

/**
 * @brief set_maxAngle eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void CylinderSensor::processSet_maxAngle(const FieldValue & sffloat,
                                         const double timestamp)
        throw (std::bad_cast) {
    this->maxAngle = dynamic_cast<const SFFloat &>(sffloat);
    this->emitEvent("maxAngle_changed", this->maxAngle, timestamp);
}

/**
 * @brief set_minAngle eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void CylinderSensor::processSet_minAngle(const FieldValue & sffloat,
                                         const double timestamp)
        throw (std::bad_cast) {
    this->minAngle = dynamic_cast<const SFFloat &>(sffloat);
    this->emitEvent("minAngle_changed", this->minAngle, timestamp);
}

/**
 * @brief set_offset eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void CylinderSensor::processSet_offset(const FieldValue & sffloat,
                                       const double timestamp)
        throw (std::bad_cast) {
    this->offset = dynamic_cast<const SFFloat &>(sffloat);
    this->emitEvent("offset_changed", this->offset, timestamp);
}


/**
 * @class DirectionalLightClass
 *
 * @brief Class object for DirectionalLight nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
DirectionalLightClass::DirectionalLightClass(Browser & browser):
        NodeClass(browser) {}

/**
 * @brief Destructor.
 */
DirectionalLightClass::~DirectionalLightClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating DirectionalLight
 *      nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by DirectionalLightClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        DirectionalLightClass::createType(const std::string & id,
                                          const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "ambientIntensity"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfcolor, "color"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "direction"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "intensity"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "on")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<DirectionalLight>(*this, id));
    Vrml97NodeTypeImpl<DirectionalLight> & directionalLightNodeType =
            static_cast<Vrml97NodeTypeImpl<DirectionalLight> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<DirectionalLight>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            directionalLightNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &DirectionalLight::processSet_ambientIntensity,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<DirectionalLight, SFFloat>
                                    (&DirectionalLight::ambientIntensity)));
        } else if (*itr == supportedInterfaces[1]) {
            directionalLightNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &DirectionalLight::processSet_color,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<DirectionalLight, SFColor>
                                    (&DirectionalLight::color)));
        } else if (*itr == supportedInterfaces[2]) {
            directionalLightNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &DirectionalLight::processSet_direction,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<DirectionalLight, SFVec3f>
                                    (&DirectionalLight::direction)));
        } else if (*itr == supportedInterfaces[3]) {
            directionalLightNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &DirectionalLight::processSet_intensity,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<DirectionalLight, SFFloat>
                                    (&DirectionalLight::intensity)));
        } else if (*itr == supportedInterfaces[4]) {
            directionalLightNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &DirectionalLight::processSet_on,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<DirectionalLight, SFBool>
                                    (&DirectionalLight::on)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class DirectionalLight
 *
 * @brief DirectionalLight node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
DirectionalLight::DirectionalLight(const NodeType & nodeType,
                                   const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractLight(nodeType, scope),
    direction(vec3f(0.0, 0.0, -1.0))
{}

/**
 * @brief Destructor.
 */
DirectionalLight::~DirectionalLight() throw () {}

/**
 * @brief Render the node.
 *
 * This should be called before rendering any sibling nodes.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void DirectionalLight::render(Viewer & viewer, const VrmlRenderContext rc)
{
    if (this->on.value) {
        viewer.insertDirLight(this->ambientIntensity.value,
                              this->intensity.value,
                              this->color.value,
                              this->direction.value);
    }
    this->clearModified();
}

/**
 * @brief set_direction eventIn handler.
 *
 * @param sfvec3f   an SFVec3f.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void DirectionalLight::processSet_direction(const FieldValue & sfvec3f,
                                            const double timestamp)
        throw (std::bad_cast) {
    this->direction = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("direction_changed", this->direction, timestamp);
}


/**
 * @class ElevationGridClass
 *
 * @brief Class object for ElevationGrid nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
ElevationGridClass::ElevationGridClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
ElevationGridClass::~ElevationGridClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating ElevationGrid nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by ElevationGridClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        ElevationGridClass::createType(const std::string & id,
                                       const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::mffloat, "set_height"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "color"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "normal"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "texCoord"),
        NodeInterface(NodeInterface::field, FieldValue::mffloat, "height"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "ccw"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "colorPerVertex"),
        NodeInterface(NodeInterface::field, FieldValue::sffloat, "creaseAngle"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "normalPerVertex"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "solid"),
        NodeInterface(NodeInterface::field, FieldValue::sfint32, "xDimension"),
        NodeInterface(NodeInterface::field, FieldValue::sffloat, "xSpacing"),
        NodeInterface(NodeInterface::field, FieldValue::sfint32, "zDimension"),
        NodeInterface(NodeInterface::field, FieldValue::sffloat, "zSpacing")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<ElevationGrid>(*this, id));
    Vrml97NodeTypeImpl<ElevationGrid> & elevationGridNodeType =
            static_cast<Vrml97NodeTypeImpl<ElevationGrid> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<ElevationGrid>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            elevationGridNodeType.addEventIn(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &ElevationGrid::processSet_height);
        } else if (*itr == supportedInterfaces[1]) {
            elevationGridNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &ElevationGrid::processSet_color,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, SFNode>
                                    (&ElevationGrid::color)));
        } else if (*itr == supportedInterfaces[2]) {
            elevationGridNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &ElevationGrid::processSet_normal,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, SFNode>
                                    (&ElevationGrid::normal)));
        } else if (*itr == supportedInterfaces[3]) {
            elevationGridNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &ElevationGrid::processSet_texCoord,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, SFNode>
                                    (&ElevationGrid::texCoord)));
        } else if (*itr == supportedInterfaces[4]) {
            elevationGridNodeType.addField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, MFFloat>
                                    (&ElevationGrid::height)));
        } else if (*itr == supportedInterfaces[5]) {
            elevationGridNodeType.addField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, SFBool>
                                    (&ElevationGrid::ccw)));
        } else if (*itr == supportedInterfaces[6]) {
            elevationGridNodeType.addField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, SFBool>
                                    (&ElevationGrid::colorPerVertex)));
        } else if (*itr == supportedInterfaces[7]) {
            elevationGridNodeType.addField(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, SFFloat>
                                    (&ElevationGrid::creaseAngle)));
        } else if (*itr == supportedInterfaces[8]) {
            elevationGridNodeType.addField(
                supportedInterfaces[8].fieldType,
                supportedInterfaces[8].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, SFBool>
                                    (&ElevationGrid::normalPerVertex)));
        } else if (*itr == supportedInterfaces[9]) {
            elevationGridNodeType.addField(
                supportedInterfaces[9].fieldType,
                supportedInterfaces[9].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, SFBool>
                                    (&ElevationGrid::solid)));
        } else if (*itr == supportedInterfaces[10]) {
            elevationGridNodeType.addField(
                supportedInterfaces[10].fieldType,
                supportedInterfaces[10].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, SFInt32>
                                    (&ElevationGrid::xDimension)));
        } else if (*itr == supportedInterfaces[11]) {
            elevationGridNodeType.addField(
                supportedInterfaces[11].fieldType,
                supportedInterfaces[11].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, SFFloat>
                                    (&ElevationGrid::xSpacing)));
        } else if (*itr == supportedInterfaces[12]) {
            elevationGridNodeType.addField(
                supportedInterfaces[12].fieldType,
                supportedInterfaces[12].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, SFInt32>
                                    (&ElevationGrid::zDimension)));
        } else if (*itr == supportedInterfaces[13]) {
            elevationGridNodeType.addField(
                supportedInterfaces[13].fieldType,
                supportedInterfaces[13].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ElevationGrid, SFFloat>
                                    (&ElevationGrid::zSpacing)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class ElevationGrid
 *
 * @brief ElevationGrid node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
ElevationGrid::ElevationGrid(const NodeType & nodeType,
                             const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractGeometry(nodeType, scope),
        ccw(true),
        colorPerVertex(true),
        normalPerVertex(true),
        solid(true),
        xDimension(0),
        xSpacing(1.0f),
        zDimension(0),
        zSpacing(1.0f) {}

/**
 * @brief Destructor.
 */
ElevationGrid::~ElevationGrid() throw () {}

/**
 * @brief Determine whether the node has been modified.
 *
 * @return @c true if the node or one of its children has been modified,
 *      @c false otherwise.
 */
bool ElevationGrid::isModified() const
{
    return (this->d_modified
            || (this->color.value && this->color.value->isModified())
            || (this->normal.value && this->normal.value->isModified())
            || (this->texCoord.value && this->texCoord.value->isModified()));
}

/**
 * @brief Propagate the bvolume dirty flag from children to parents.
 *
 * @param path  stack of ancestor nodes.
 * @param flags 1 indicates normal modified flag, 2 indicates the
 *              bvolume dirty flag, 3 indicates both.
 */
void ElevationGrid::updateModified(NodePath & path, int flags)
{
    if (this->isModified()) { markPathModified(path, true); }
    path.push_front(this);
    if (this->color.value) { this->color.value->updateModified(path); }
    if (this->normal.value) { this->normal.value->updateModified(path); }
    if (this->texCoord.value) { this->texCoord.value->updateModified(path); }
    path.pop_front();
}

/**
 * @brief Insert this geometry into @p viewer's display list.
 *
 * @param viewer    a Viewer.
 * @param context   the rendering context.
 */
Viewer::Object ElevationGrid::insertGeometry(Viewer & viewer,
                                             const VrmlRenderContext context)
{
    Viewer::Object obj = 0;

    if (!this->height.value.empty()) {
        using std::vector;

        ColorNode * const colorNode = this->color.value->toColor();
        const vector<OpenVRML::color> & color = colorNode
                                              ? colorNode->getColor()
                                              : vector<OpenVRML::color>();

        NormalNode * const normalNode = this->normal.value->toNormal();
        const vector<vec3f> & normal = normalNode
                                     ? normalNode->getVector()
                                     : vector<vec3f>();

        TextureCoordinateNode * const texCoordNode =
                this->texCoord.value->toTextureCoordinate();
        const vector<vec2f> & texCoord = texCoordNode
                                       ? texCoordNode->getPoint()
                                       : vector<vec2f>();
        // insert geometry
        unsigned int optMask = 0;
        if (this->ccw.value) {
            optMask |= Viewer::MASK_CCW;
        }
        if (this->solid.value) {
            optMask |= Viewer::MASK_SOLID;
        }
        if (this->colorPerVertex.value) {
            optMask |= Viewer::MASK_COLOR_PER_VERTEX;
        }
        if (this->normalPerVertex.value) {
            optMask |= Viewer::MASK_NORMAL_PER_VERTEX;
        }

        obj = viewer.insertElevationGrid(optMask,
                                         this->height.value,
                                         this->xDimension.value,
                                         this->zDimension.value,
                                         this->xSpacing.value,
                                         this->zSpacing.value,
                                         color,
                                         normal,
                                         texCoord);
    }

    if (this->color.value) { this->color.value->clearModified(); }
    if (this->normal.value) { this->normal.value->clearModified(); }
    if (this->texCoord.value) { this->texCoord.value->clearModified(); }

    return obj;
}

/**
 * @brief set_color eventIn handler.
 *
 * @param sfnode    an SFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void ElevationGrid::processSet_color(const FieldValue & sfnode,
                                     const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->color = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("color_changed", this->color, timestamp);
}

/**
 * @brief set_height eventIn handler.
 *
 * @param mffloat   an MFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mffloat is not an MFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void ElevationGrid::processSet_height(const FieldValue & mffloat,
                                      const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->height = dynamic_cast<const MFFloat &>(mffloat);
    this->setModified();
    this->emitEvent("height_changed", this->height, timestamp);
}

/**
 * @brief set_normal eventIn handler.
 *
 * @param sfnode    an SFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void ElevationGrid::processSet_normal(const FieldValue & sfnode,
                                      const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->normal = dynamic_cast<const SFNode &>(sfnode);
    this->emitEvent("normal_changed", this->normal, timestamp);
}

/**
 * @brief set_texCoord eventIn handler.
 *
 * @param sfnode    an SFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void ElevationGrid::processSet_texCoord(const FieldValue & sfnode,
                                        const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->texCoord = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("texCoord_changed", this->texCoord, timestamp);
}


/**
 * @class ExtrusionClass
 *
 * @brief Class object for Extrusion nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
ExtrusionClass::ExtrusionClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
ExtrusionClass::~ExtrusionClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Extrusion nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by ExtrusionClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        ExtrusionClass::createType(const std::string & id,
                                   const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::mfvec2f, "set_crossSection"),
        NodeInterface(NodeInterface::eventIn, FieldValue::mfrotation, "set_orientation"),
        NodeInterface(NodeInterface::eventIn, FieldValue::mfvec2f, "set_scale"),
        NodeInterface(NodeInterface::eventIn, FieldValue::mfvec3f, "set_spine"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "beginCap"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "ccw"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "convex"),
        NodeInterface(NodeInterface::field, FieldValue::sffloat, "creaseAngle"),
        NodeInterface(NodeInterface::field, FieldValue::mfvec2f, "crossSection"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "endCap"),
        NodeInterface(NodeInterface::field, FieldValue::mfrotation, "orientation"),
        NodeInterface(NodeInterface::field, FieldValue::mfvec2f, "scale"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "solid"),
        NodeInterface(NodeInterface::field, FieldValue::mfvec3f, "spine")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Extrusion>(*this, id));
    Vrml97NodeTypeImpl<Extrusion> & extrusionNodeType =
            static_cast<Vrml97NodeTypeImpl<Extrusion> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Extrusion>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            extrusionNodeType.addEventIn(supportedInterfaces[0].fieldType,
                                         supportedInterfaces[0].id,
                                         &Extrusion::processSet_crossSection);
        } else if (*itr == supportedInterfaces[1]) {
            extrusionNodeType.addEventIn(supportedInterfaces[1].fieldType,
                                         supportedInterfaces[1].id,
                                         &Extrusion::processSet_orientation);
        } else if (*itr == supportedInterfaces[2]) {
            extrusionNodeType.addEventIn(supportedInterfaces[2].fieldType,
                                         supportedInterfaces[2].id,
                                         &Extrusion::processSet_scale);
        } else if (*itr == supportedInterfaces[3]) {
            extrusionNodeType.addEventIn(supportedInterfaces[3].fieldType,
                                         supportedInterfaces[3].id,
                                         &Extrusion::processSet_spine);
        } else if (*itr == supportedInterfaces[4]) {
            extrusionNodeType.addField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Extrusion, SFBool>
                                    (&Extrusion::beginCap)));
        } else if (*itr == supportedInterfaces[5]) {
            extrusionNodeType.addField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Extrusion, SFBool>
                                    (&Extrusion::ccw)));
        } else if (*itr == supportedInterfaces[6]) {
            extrusionNodeType.addField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Extrusion, SFBool>
                                    (&Extrusion::convex)));
        } else if (*itr == supportedInterfaces[7]) {
            extrusionNodeType.addField(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Extrusion, SFFloat>
                                    (&Extrusion::creaseAngle)));
        } else if (*itr == supportedInterfaces[8]) {
            extrusionNodeType.addField(
                supportedInterfaces[8].fieldType,
                supportedInterfaces[8].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Extrusion, MFVec2f>
                                    (&Extrusion::crossSection)));
        } else if (*itr == supportedInterfaces[9]) {
            extrusionNodeType.addField(
                supportedInterfaces[9].fieldType,
                supportedInterfaces[9].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Extrusion, SFBool>
                                    (&Extrusion::endCap)));
        } else if (*itr == supportedInterfaces[10]) {
            extrusionNodeType.addField(
                supportedInterfaces[10].fieldType,
                supportedInterfaces[10].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Extrusion, MFRotation>
                                    (&Extrusion::orientation)));
        } else if (*itr == supportedInterfaces[11]) {
            extrusionNodeType.addField(
                supportedInterfaces[11].fieldType,
                supportedInterfaces[11].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Extrusion, MFVec2f>
                                    (&Extrusion::scale)));
        } else if (*itr == supportedInterfaces[12]) {
            extrusionNodeType.addField(
                supportedInterfaces[12].fieldType,
                supportedInterfaces[12].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Extrusion, SFBool>
                                    (&Extrusion::solid)));
        } else if (*itr == supportedInterfaces[13]) {
            extrusionNodeType.addField(
                supportedInterfaces[13].fieldType,
                supportedInterfaces[13].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Extrusion, MFVec3f>
                                    (&Extrusion::spine)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

namespace {
    const vec2f extrusionDefaultCrossSection_[] = { vec2f(1.0, 1.0),
                                                    vec2f(1.0, -1.0),
                                                    vec2f(-1.0, -1.0),
                                                    vec2f(-1.0, 1.0),
                                                    vec2f(1.0, 1.0) };
    const vec2f extrusionDefaultScale_[] = { vec2f(1.0, 1.0) };
    const rotation extrusionDefaultOrientation_[] =
            { rotation(0.0, 0.0, 1.0, 0.0) };
    const vec3f extrusionDefaultSpine_[] = { vec3f(0.0, 0.0, 0.0),
                                             vec3f(0.0, 1.0, 0.0) };
}

/**
 * @class Extrusion
 *
 * @brief Extrusion node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
Extrusion::Extrusion(const NodeType & nodeType,
                     const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractGeometry(nodeType, scope),
    beginCap(true),
    ccw(true),
    convex(true),
    creaseAngle(0),
    crossSection(extrusionDefaultCrossSection_,
                 extrusionDefaultCrossSection_ + 5),
    endCap(true),
    orientation(extrusionDefaultOrientation_, extrusionDefaultOrientation_ + 1),
    scale(extrusionDefaultScale_, extrusionDefaultScale_ + 1),
    solid(true),
    spine(extrusionDefaultSpine_, extrusionDefaultSpine_ + 2)
{}

/**
 * @brief Destructor.
 */
Extrusion::~Extrusion() throw () {}

/**
 * @brief Insert this geometry into @p viewer's display list.
 *
 * @param viewer    a Viewer.
 * @param context   the rendering context.
 */
Viewer::Object Extrusion::insertGeometry(Viewer & viewer,
                                         const VrmlRenderContext context)
{
    Viewer::Object obj = 0;
    if (this->crossSection.value.size() > 0 && this->spine.value.size() > 1) {

        unsigned int optMask = 0;
        if (this->ccw.value)        { optMask |= Viewer::MASK_CCW; }
        if (this->convex.value)     { optMask |= Viewer::MASK_CONVEX; }
        if (this->solid.value)      { optMask |= Viewer::MASK_SOLID; }
        if (this->beginCap.value)   { optMask |= Viewer::MASK_BOTTOM; }
        if (this->endCap.value)     { optMask |= Viewer::MASK_TOP; }

        obj = viewer.insertExtrusion(optMask,
                                     this->spine.value,
                                     this->crossSection.value,
                                     this->orientation.value,
                                     this->scale.value);
    }

    return obj;
}

/**
 * @brief set_crossSection eventIn handler.
 *
 * @param mfvec2f   an MFVec2f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfvec2f is not an MFVec2f.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Extrusion::processSet_crossSection(const FieldValue & mfvec2f,
                                        const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->crossSection = dynamic_cast<const MFVec2f &>(mfvec2f);
    this->setModified();
}

/**
 * @brief set_orientation field mutator.
 *
 * @param mfrotation   an MFRotation value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfrotation is not an MFRotation.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Extrusion::processSet_orientation(const FieldValue & mfrotation,
                                       const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->orientation = dynamic_cast<const MFRotation &>(mfrotation);
    this->setModified();
}

/**
 * @brief set_scale eventIn handler.
 *
 * @param mfvec2f   an MFVec2f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfvec2f is not an MFVec2f.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Extrusion::processSet_scale(const FieldValue & mfvec2f,
                                 const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->scale = dynamic_cast<const MFVec2f &>(mfvec2f);
    this->setModified();
}

/**
 * @brief set_spine eventIn handler.
 *
 * @param mfvec3f   an MFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfvec2f is not an MFVec3f.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Extrusion::processSet_spine(const FieldValue & mfvec3f,
                                 const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->spine = dynamic_cast<const MFVec3f &>(mfvec3f);
    this->setModified();
}


/**
 * @class FogClass
 *
 * @brief Class object for Fog nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
FogClass::FogClass(Browser & browser):
    NodeClass(browser),
    first(0)
{}

/**
 * @brief Destructor.
 */
FogClass::~FogClass() throw () {}

/**
 * @brief Set the first Fog node in the world.
 *
 * The first Fog node in the world is used for the initial fog.
 * This method is used by Fog::do_initialize.
 *
 * @param fog   a Fog node.
 */
void FogClass::setFirst(Fog & fog) throw ()
{
    this->first = &fog;
}

/**
 * @brief Check to see if the first node has been set.
 *
 * This method is used by Fog::do_initialize.
 *
 * @return @c true if the first node has already been set; @c false otherwise.
 */
bool FogClass::hasFirst() const throw ()
{
    return this->first;
}

/**
 * @brief Push a Fog on the top of the bound node stack.
 *
 * @param fog       the node to bind.
 * @param timestamp the current time.
 */
void FogClass::bind(Fog & fog, const double timestamp) throw (std::bad_alloc)
{
    using std::find;

    //
    // If the node is already the active node, do nothing.
    //
    if (!this->boundNodes.empty() && &fog == this->boundNodes.back()) {
        return;
    }

    //
    // If the node is already on the stack, remove it.
    //
    const BoundNodes::iterator pos =
        find(this->boundNodes.begin(), this->boundNodes.end(), &fog);
    if (pos != this->boundNodes.end()) { this->boundNodes.erase(pos); }

    //
    // Send FALSE from the currently active node's isBound.
    //
    if (!this->boundNodes.empty()) {
        Fog & current = dynamic_cast<Fog &>(*this->boundNodes.back());
        current.bound.value = false;
        current.emitEvent("isBound", current.bound, timestamp);
    }

    //
    // Push the node to the top of the stack, and have it send isBound TRUE.
    //
    this->boundNodes.push_back(&fog);
    fog.bound.value = true;
    fog.emitEvent("isBound", fog.bound, timestamp);
}

/**
 * @brief Remove a Fog from the bound node stack.
 *
 * @param fog       the node to unbind.
 * @param timestamp the current time.
 */
void FogClass::unbind(Fog & fog, const double timestamp) throw ()
{
    const BoundNodes::iterator pos =
            std::find(this->boundNodes.begin(), this->boundNodes.end(), &fog);
    if (pos != this->boundNodes.end()) {
        fog.bound.value = false;
        fog.emitEvent("isBound", fog.bound, timestamp);

        if (pos == this->boundNodes.end() - 1
                && this->boundNodes.size() > 1) {
            Fog & newActive =
                    dynamic_cast<Fog &>(**(this->boundNodes.end() - 2));
            newActive.bound.value = true;
            newActive.emitEvent("isBound", newActive.bound, timestamp);
        }
        this->boundNodes.erase(pos);
    }
}

/**
 * @brief NodeClass-specific initialization.
 *
 * @param initialViewpoint  the ViewpointNode that should be bound initially.
 * @param timestamp         the current time.
 */
void FogClass::initialize(ViewpointNode * initialViewpoint,
                          const double timestamp)
    throw ()
{
    if (this->first) {
        this->first->processEvent("set_bind", SFBool(true), timestamp);
    }
}

/**
 * @brief NodeClass-specific rendering.
 *
 * Render the active Fog node.
 *
 * @param viewer    a Viewer.
 */
void FogClass::render(Viewer & viewer) throw ()
{
    if (!this->boundNodes.empty()) {
        Fog & fog = dynamic_cast<Fog &>(*this->boundNodes.back());
        viewer.setFog(fog.color.value,
                      fog.visibilityRange.value,
                      fog.fogType.value.c_str());
    }
}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Fog nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by FogClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr FogClass::createType(const std::string & id,
                                       const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::sfbool, "set_bind"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfcolor, "color"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfstring, "fogType"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "visibilityRange"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isBound")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Fog>(*this, id));
    Vrml97NodeTypeImpl<Fog> & fogNodeType =
            static_cast<Vrml97NodeTypeImpl<Fog> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Fog>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            fogNodeType.addEventIn(supportedInterfaces[0].fieldType,
                                   supportedInterfaces[0].id,
                                   &Fog::processSet_bind);
        } else if (*itr == supportedInterfaces[1]) {
            fogNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &Fog::processSet_color,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Fog, SFColor>
                                    (&Fog::color)));
        } else if (*itr == supportedInterfaces[2]) {
            fogNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &Fog::processSet_fogType,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Fog, SFString>
                                    (&Fog::fogType)));
        } else if (*itr == supportedInterfaces[3]) {
            fogNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &Fog::processSet_visibilityRange,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Fog, SFFloat>
                                    (&Fog::visibilityRange)));
        } else if (*itr == supportedInterfaces[4]) {
            fogNodeType.addEventOut(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Fog, SFBool>
                                    (&Fog::bound)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Fog
 *
 * @brief Fog node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
Fog::Fog(const NodeType & nodeType,
         const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope),
    color(OpenVRML::color(1.0, 1.0, 1.0)),
    fogType("LINEAR"),
    visibilityRange(0.0),
    bound(false)
{}

/**
 * @brief Destructor.
 */
Fog::~Fog() throw ()
{}

/**
 * @brief Initialize.
 *
 * @param timestamp the current time.
 */
void Fog::do_initialize(const double timestamp) throw ()
{
    FogClass & nodeClass = static_cast<FogClass &>(this->nodeType.nodeClass);
    if (!nodeClass.hasFirst()) { nodeClass.setFirst(*this); }
}

/**
 * @brief Shut down.
 *
 * Calls FogClass::unbind to unbind the node if it is bound.
 *
 * @param timestamp the current time.
 */
void Fog::do_shutdown(const double timestamp) throw ()
{
    FogClass & nodeClass = static_cast<FogClass &>(this->nodeType.nodeClass);
    nodeClass.unbind(*this, timestamp);
}

/**
 * @brief set_bind eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfbool is not an SFBool value.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Fog::processSet_bind(const FieldValue & sfbool, const double timestamp)
        throw (std::bad_cast, std::bad_alloc)
{
    const SFBool & bind = dynamic_cast<const SFBool &>(sfbool);
    FogClass & nodeClass = static_cast<FogClass &>(this->nodeType.nodeClass);
    if (bind.value) {
        nodeClass.bind(*this, timestamp);
    } else {
        nodeClass.unbind(*this, timestamp);
    }
}

/**
 * @brief set_color eventIn handler.
 *
 * @param sfcolor   an SFColor value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfcolor is not an SFColor value.
 */
void Fog::processSet_color(const FieldValue & sfcolor, const double timestamp)
        throw (std::bad_cast) {
    this->color = dynamic_cast<const SFColor &>(sfcolor);
    this->setModified();
    this->emitEvent("color_changed", this->color, timestamp);
}

/**
 * @brief set_fogType eventIn handler.
 *
 * @param sfstring  an SFString value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfstring is not an SFString value.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Fog::processSet_fogType(const FieldValue & sfstring,
                             const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->fogType = dynamic_cast<const SFString &>(sfstring);
    this->setModified();
    this->emitEvent("fogType_changed", this->fogType, timestamp);
}

/**
 * @brief set_visibilityRange eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat value.
 */
void Fog::processSet_visibilityRange(const FieldValue & sffloat,
                                     const double timestamp)
        throw (std::bad_cast) {
    this->visibilityRange = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("visibilityRange_changed", this->visibilityRange,
                    timestamp);
}


/**
 * @class FontStyleClass
 *
 * @brief Class object for FontStyle nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
FontStyleClass::FontStyleClass(Browser & browser): NodeClass(browser) {}

FontStyleClass::~FontStyleClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating FontStyle nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by FontStyleClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        FontStyleClass::createType(const std::string & id,
                                   const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::field, FieldValue::mfstring, "family"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "horizontal"),
        NodeInterface(NodeInterface::field, FieldValue::mfstring, "justify"),
        NodeInterface(NodeInterface::field, FieldValue::sfstring, "language"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "leftToRight"),
        NodeInterface(NodeInterface::field, FieldValue::sffloat, "size"),
        NodeInterface(NodeInterface::field, FieldValue::sffloat, "spacing"),
        NodeInterface(NodeInterface::field, FieldValue::sfstring, "style"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "topToBottom")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<FontStyle>(*this, id));
    Vrml97NodeTypeImpl<FontStyle> & fontStyleNodeType =
            static_cast<Vrml97NodeTypeImpl<FontStyle> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<FontStyle>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            fontStyleNodeType.addField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<FontStyle, MFString>
                                    (&FontStyle::family)));
        } else if (*itr == supportedInterfaces[1]) {
            fontStyleNodeType.addField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<FontStyle, SFBool>
                                    (&FontStyle::horizontal)));
        } else if (*itr == supportedInterfaces[2]) {
            fontStyleNodeType.addField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<FontStyle, MFString>
                                    (&FontStyle::justify)));
        } else if (*itr == supportedInterfaces[3]) {
            fontStyleNodeType.addField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<FontStyle, SFString>
                                    (&FontStyle::language)));
        } else if (*itr == supportedInterfaces[4]) {
            fontStyleNodeType.addField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<FontStyle, SFBool>
                                    (&FontStyle::leftToRight)));
        } else if (*itr == supportedInterfaces[5]) {
            fontStyleNodeType.addField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<FontStyle, SFFloat>
                                    (&FontStyle::size)));
        } else if (*itr == supportedInterfaces[6]) {
            fontStyleNodeType.addField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<FontStyle, SFFloat>
                                    (&FontStyle::spacing)));
        } else if (*itr == supportedInterfaces[7]) {
            fontStyleNodeType.addField(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<FontStyle, SFString>
                                    (&FontStyle::style)));
        } else if (*itr == supportedInterfaces[8]) {
            fontStyleNodeType.addField(
                supportedInterfaces[8].fieldType,
                supportedInterfaces[8].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<FontStyle, SFBool>
                                    (&FontStyle::topToBottom)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class FontStyle
 *
 * @brief FontStyle node instances.
 */

namespace {
    const std::string fontStyleInitFamily_[] = { "SERIF" };
    const std::string fontStyleInitJustify_[] = { "BEGIN", "FIRST" };
}

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
FontStyle::FontStyle(const NodeType & nodeType,
                     const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    FontStyleNode(nodeType, scope),
    family(fontStyleInitFamily_, fontStyleInitFamily_ + 1),
    horizontal(true),
    justify(fontStyleInitJustify_, fontStyleInitJustify_ + 2),
    leftToRight(true),
    size(1.0),
    spacing(1.0),
    style("PLAIN"),
    topToBottom(true)
{}

/**
 * @brief Destructor.
 */
FontStyle::~FontStyle() throw ()
{}

/**
 * @brief Get the list of font families.
 *
 * @return an MFString containing the font families that may be used for this
 *      FontStyle.
 */
const std::vector<std::string> & FontStyle::getFamily() const throw ()
{
    return this->family.value;
}

/**
 * @brief Get the flag indicating whether the text should be horizontal or
 *      vertical.
 *
 * @return @c true if the text should be horizontal, or @c false if the text
 *      should be vertical.
 */
bool FontStyle::getHorizontal() const throw ()
{
    return this->horizontal.value;
}

/**
 * @brief Get the descriptor for the text justification.
 *
 * @return a string array describing the characteristics of the text
 *      justification.
 */
const std::vector<std::string> & FontStyle::getJustify() const throw ()
{
    return this->justify.value;
}

/**
 * @brief Get the language code.
 *
 * @return the language code.
 */
const std::string & FontStyle::getLanguage() const throw ()
{
    return this->language.value;
}

/**
 * @brief Get the flag indicating whether the text should be rendered
 *      left-to-right.
 *
 * @return @c true if the text should be rendered left-to-right, or @c false if
 *      the text should be rendered right-to-left.
 */
bool FontStyle::getLeftToRight() const throw ()
{
    return this->leftToRight.value;
}

/**
 * @brief Get the size of the text.
 *
 * @return the size of the text.
 */
float FontStyle::getSize() const throw ()
{
    return this->size.value;
}

/**
 * @brief Get the spacing for the text.
 *
 * @return the spacing for the text.
 */
float FontStyle::getSpacing() const throw ()
{
    return this->spacing.value;
}

/**
 * @brief Get the style for the text.
 *
 * @return an string descriptor of the text style.
 */
const std::string & FontStyle::getStyle() const throw ()
{
    return this->style.value;
}

/**
 * @brief Get the flag indicating whether the text should be rendered
 *      top-to-bottom.
 *
 * @return @c true if the text should be rendered top-to-bottom, or @c false if
 *      the text should be rendered bottom-to-top.
 */
bool FontStyle::getTopToBottom() const throw ()
{
    return this->topToBottom.value;
}


/**
 * @class GroupClass
 *
 * @brief Class object for Group nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this node class object.
 */
GroupClass::GroupClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
GroupClass::~GroupClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Group nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by GroupClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr GroupClass::createType(const std::string & id,
                                         const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::mfnode, "addChildren"),
        NodeInterface(NodeInterface::eventIn, FieldValue::mfnode, "removeChildren"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfnode, "children"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "bboxCenter"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "bboxSize")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Group>(*this, id));
    Vrml97NodeTypeImpl<Group> & groupNodeType =
            static_cast<Vrml97NodeTypeImpl<Group> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Group>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            groupNodeType.addEventIn(supportedInterfaces[0].fieldType,
                                     supportedInterfaces[0].id,
                                     &Group::processAddChildren);
        } else if (*itr == supportedInterfaces[1]) {
            groupNodeType.addEventIn(supportedInterfaces[1].fieldType,
                                     supportedInterfaces[1].id,
                                     &Group::processRemoveChildren);
        } else if (*itr == supportedInterfaces[2]) {
            groupNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &Group::processSet_children,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Group, MFNode>
                                    (&Group::children)));
        } else if (*itr == supportedInterfaces[3]) {
            groupNodeType.addField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Group, SFVec3f>
                                    (&Group::bboxCenter)));
        } else if (*itr == supportedInterfaces[4]) {
            groupNodeType.addField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Group, SFVec3f>
                                (&Group::bboxSize)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Group
 *
 * @brief Represents Group node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node.
 * @param scope     the Scope to which the node belongs.
 */
Group::Group(const NodeType & nodeType,
             const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    ChildNode(nodeType, scope),
    GroupingNode(nodeType, scope),
    bboxSize(vec3f(-1.0, -1.0, -1.0)),
    viewerObject(0)
{
    this->setBVolumeDirty(true);
}

/**
 * @brief Destructor.
 */
Group::~Group() throw () {
    // delete viewerObject...
}

/**
 * @brief addChildren eventIn handler.
 *
 * @param mfnode    an MFNode containing nodes to add to this Group.
 * @param timestamp the current timestamp
 *
 * @exception std::bad_cast     if @p mfnode is not an MFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Group::processAddChildren(const FieldValue & mfnode,
                               const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    const MFNode & newChildren = dynamic_cast<const MFNode &>(mfnode);
    size_t nNow = this->children.value.size();

    for (size_t i = 0; i < newChildren.value.size(); ++i) {
        const NodePtr & child = newChildren.value[i];
        if (child && child->toChild()) {
            this->children.value.push_back(child);
            child->relocate();
        } else {
            theSystem->error(
                "Error: Attempt to add a %s node as a child of a %s node.\n",
                child->nodeType.id.c_str(), this->nodeType.id.c_str());
        }
    }

    if (nNow != this->children.value.size()) {
        setModified();
        this->setBVolumeDirty(true);
    }
}

/**
 * @brief removeChildren eventIn handler.
 *
 * @param mfnode    an MFNode containing nodes to remove from this Group.
 * @param timestamp the current timestamp
 *
 * @exception std::bad_cast     if @p mfnode is not an MFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Group::processRemoveChildren(const FieldValue & mfnode,
                                  const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    const MFNode & childrenToRemove = dynamic_cast<const MFNode &>(mfnode);
    const size_t oldLength = this->children.value.size();

    for (size_t i = 0; i < childrenToRemove.value.size(); ++i) {
        const NodePtr & node = childrenToRemove.value[i];
        if (node) {
            using std::remove;
            using std::vector;
            const vector<NodePtr>::iterator begin(this->children.value.begin());
            const vector<NodePtr>::iterator end(this->children.value.end());
            this->children.value.erase(remove(begin, end, node), end);
        }
    }

    if (oldLength != this->children.value.size()) {
        setModified();
        this->setBVolumeDirty(true);
    }
}

/**
 * @brief set_children eventIn handler.
 *
 * @param mfnode    an MFNode containing nodes for this Group.
 * @param timestamp the current timestamp
 *
 * @exception std::bad_cast     if @p mfnode is not an MFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Group::processSet_children(const FieldValue & mfnode,
                                const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->children = dynamic_cast<const MFNode &>(mfnode);

    for (size_t i = 0; i < this->children.value.size(); ++i) {
        if (children.value[i]) {
            children.value[i]->relocate();
        }
    }

    this->setModified();
    this->setBVolumeDirty(true);
    this->emitEvent("children_changed", this->children, timestamp);
}

/**
 * @brief Determine whether the node has been modified.
 *
 * @return @c true if the node or one of its children has been modified,
 *      @c false otherwise.
 */
bool Group::isModified() const {
    if (this->d_modified) { return true; }

    for (size_t i = 0; i < this->children.value.size(); ++i) {
        if (this->children.value[i]->isModified()) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Propagate the bvolume dirty flag from children to parents.
 *
 * @param path  stack of ancestor nodes.
 * @param flags 1 indicates normal modified flag, 2 indicates the
 *              bvolume dirty flag, 3 indicates both.
 */
void Group::updateModified(NodePath & path, int flags) {
    // if the mark_modifed short circuit doesn't
    // pan out, we should be a little smarter here...
    if (this->isModified()) { markPathModified(path, true, flags); }
    path.push_front(this);
    for (size_t i = 0; i < this->children.value.size(); ++i) {
        this->children.value[i]->updateModified(path, flags);
    }
    path.pop_front();
}

/**
 * @brief Render the node.
 *
 * Render each of the children.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void Group::render(Viewer & viewer, VrmlRenderContext context)
{
    if (context.getCullFlag() != BVolume::inside) {
        const BSphere * bs = static_cast<const BSphere *>(this->getBVolume());
        BSphere bv_copy(*bs);
        bv_copy.transform(context.getMatrix());
        BVolume::Intersection r = viewer.intersectViewVolume(bv_copy);
        if (context.getDrawBSpheres()) { viewer.drawBSphere(*bs, r); }
        if (r == BVolume::outside) { return; }
        if (r == BVolume::inside) { context.setCullFlag(BVolume::inside); }
    }
    this->renderNoCull(viewer, context);
}


/**
 * because children will already have done the culling, we don't need
 * to repeat it here.
 */
void Group::renderNoCull(Viewer & viewer, VrmlRenderContext context) {
    if (this->viewerObject && this->isModified()) {
        viewer.removeObject(this->viewerObject);
        this->viewerObject = 0;
    }

    if (this->viewerObject) {
        viewer.insertReference(this->viewerObject);
    } else if (this->children.value.size() > 0) {
        int i, n = this->children.value.size();
        int nSensors = 0;

        this->viewerObject = viewer.beginObject(this->getId().c_str());

        // Draw nodes that impact their siblings (DirectionalLights,
        // TouchSensors, any others? ...)
        for (i = 0; i < n; ++i) {
          const NodePtr & kid = this->children.value[i];

            if (kid->toLight()
                    && !(kid->toPointLight() || kid->toSpotLight())) {
                kid->render(viewer, context);
            } else if ((kid->toTouchSensor()
                        && kid->toTouchSensor()->isEnabled())
                    || (kid->toPlaneSensor()
                        && kid->toPlaneSensor()->isEnabled())
                    || (kid->toCylinderSensor()
                        && kid->toCylinderSensor()->isEnabled())
                    || (kid->toSphereSensor()
                        && kid->toSphereSensor()->isEnabled())) {
                if (++nSensors == 1) { viewer.setSensitive(this); }
            }
        }

        // Do the rest of the children (except the scene-level lights)
        for (i = 0; i<n; ++i) {
            const NodePtr & child = this->children.value[i];
            if (!(child->toLight()
//                    || child->toPlaneSensor()
//                    || child->toCylinderSensor()
//                    || child->toSphereSensor()
                    || child->toTouchSensor())) {
                child->render(viewer, context);
            }
        }

        // Turn off sensitivity
        if (nSensors > 0) { viewer.setSensitive(0); }

        viewer.endObject();
    }

    this->clearModified();
}

/**
 * @brief Get the children in the scene graph.
 *
 * @return the child nodes in the scene graph.
 */
const std::vector<NodePtr> & Group::getChildren() const throw ()
{
    return this->children.value;
}

/**
 * Pass on to enabled touchsensor child.
 */
void Group::activate(double time, bool isOver, bool isActive, double *p)
{
    for (size_t i = 0; i < this->children.value.size(); ++i) {
        const NodePtr & node = this->children.value[i];
        if (node) {
            if (node->toTouchSensor() && node->toTouchSensor()->isEnabled()) {
                node->toTouchSensor()->activate(time, isOver, isActive, p);
            } else if (node->toPlaneSensor()
                    && node->toPlaneSensor()->isEnabled()) {
                node->toPlaneSensor()->activate(time, isActive, p);
            } else if (node->toCylinderSensor()
                    && node->toCylinderSensor()->isEnabled()) {
                node->toCylinderSensor()->activate(time, isActive, p);
            } else if (node->toSphereSensor()
                    && node->toSphereSensor()->isEnabled()) {
                node->toSphereSensor()->activate(time, isActive, p);
            }
        }
    }
}

/**
 * @brief Get the bounding volume.
 *
 * @return the bounding volume associated with the node.
 */
const BVolume * Group::getBVolume() const
{
  if (this->isBVolumeDirty())
    ((Group*)this)->recalcBSphere();
  return &this->bsphere;
}

/**
 * @brief Recalculate the bounding volume.
 */
void Group::recalcBSphere() {
    this->bsphere.reset();
    for (size_t i = 0; i < this->children.value.size(); ++i) {
        const NodePtr & node = this->children.value[i];
        if (node) {
            const BVolume * const ci_bv = node->getBVolume();
            if (ci_bv) { this->bsphere.extend(*ci_bv); }
        }
    }
    this->setBVolumeDirty(false);
}


/**
 * @class ImageTextureClass
 *
 * @brief Class object for ImageTexture nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this node class object.
 */
ImageTextureClass::ImageTextureClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
ImageTextureClass::~ImageTextureClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating ImageTexture nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by ImageTextureClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr ImageTextureClass::
        createType(const std::string & id,
                   const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "url"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "repeatS"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "repeatT")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<ImageTexture>(*this, id));
    Vrml97NodeTypeImpl<ImageTexture> & imageTextureNodeType =
            static_cast<Vrml97NodeTypeImpl<ImageTexture> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<ImageTexture>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            imageTextureNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &ImageTexture::processSet_url,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ImageTexture, MFString>
                                    (&ImageTexture::url)));
        } else if (*itr == supportedInterfaces[1]) {
            imageTextureNodeType.addField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ImageTexture, SFBool>
                                    (&ImageTexture::repeatS)));
        } else if (*itr == supportedInterfaces[2]) {
            imageTextureNodeType.addField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ImageTexture, SFBool>
                                    (&ImageTexture::repeatT)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class ImageTexture
 *
 * @brief Represents ImageTexture node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node.
 * @param scope     the Scope to which the node belongs.
 */
ImageTexture::ImageTexture(const NodeType & nodeType,
                           const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractTexture(nodeType, scope),
        image(0),
        texObject(0) {}

/**
 * @brief Destructor.
 */
ImageTexture::~ImageTexture() throw () {
    delete this->image;
    // delete texObject...
}

/**
 * @brief Render the node.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void ImageTexture::render(Viewer & viewer, VrmlRenderContext context)
{
    if (isModified()) {
        if (this->image) {
            delete this->image;        // URL is the only modifiable bit
            this->image = 0;
        }
        if (this->texObject) {
            viewer.removeTextureObject(this->texObject);
            this->texObject = 0;
        }
    }

    // should probably read the image during addToScene...
    // should cache on url so multiple references to the same file are
    // loaded just once... of course world authors should just DEF/USE
    // them...
    if (!this->image && this->url.value.size() > 0) {
        Doc2 baseDoc(this->getScene()->getURI());
        this->image = new Image;
        if (!this->image->tryURLs(this->url, &baseDoc)) {
            theSystem->error("Couldn't read ImageTexture from URL %s\n",
                             this->url.value[0].c_str());
        }
    }

    // Check texture cache
    if (this->texObject && this->image) {
        viewer.insertTextureReference(this->texObject, this->image->nc());
    } else {
        unsigned char *pix;

        if (this->image && (pix = this->image->pixels())) {
            // Ensure the image dimensions are powers of two
            int sizes[] = { 2, 4, 8, 16, 32, 64, 128, 256 };
            int nSizes = sizeof(sizes) / sizeof(int);
            int w = this->image->w();
            int h = this->image->h();
            int i, j;
            for (i = 0; i < nSizes; ++i) { if (w < sizes[i]) { break; } }
            for (j = 0; j < nSizes; ++j) { if (h < sizes[j]) { break; } }

            if (i > 0 && j > 0) {
                // Always scale images down in size and reuse the same pixel
                // memory. This can cause some ugliness...
                if (w != sizes[i - 1] || h != sizes[j - 1]) {
                    viewer.scaleTexture(w, h, sizes[i - 1], sizes[j - 1],
                                        this->image->nc(), pix);
                    this->image->setSize(sizes[i - 1], sizes[j - 1]);
                }

                this->texObject = viewer.insertTexture(this->image->w(),
                                                       this->image->h(),
                                                       this->image->nc(),
                                                       this->repeatS.value,
                                                       this->repeatT.value,
                                                       pix,
                                                       true);
            }
        }
    }

    this->clearModified();
}

size_t ImageTexture::nComponents() const throw () {
    return this->image ? this->image->nc() : 0;
}

size_t ImageTexture::width() const throw () {
    return this->image ? this->image->w() : 0;
}

size_t ImageTexture::height() const throw () {
    return this->image ? this->image->h() : 0;
}

size_t ImageTexture::nFrames() const throw () { return 0; }

const unsigned char * ImageTexture::pixels() const throw () {
    return this->image ? this->image->pixels() : 0;
}

/**
 * @brief set_url eventIn handler.
 *
 * @param mfstring  an MFString value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void ImageTexture::processSet_url(const FieldValue & mfstring,
                                  const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->url = dynamic_cast<const MFString &>(mfstring);
    this->setModified();
    this->emitEvent("url_changed", this->url, timestamp);
}


/**
 * @class IndexedFaceSetClass
 *
 * @brief Class object for IndexedFaceSet nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this node class object.
 */
IndexedFaceSetClass::IndexedFaceSetClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
IndexedFaceSetClass::~IndexedFaceSetClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating IndexedFaceSet nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by IndexedFaceSetClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        IndexedFaceSetClass::createType(const std::string & id,
                                        const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::mfint32, "set_colorIndex"),
        NodeInterface(NodeInterface::eventIn, FieldValue::mfint32, "set_coordIndex"),
        NodeInterface(NodeInterface::eventIn, FieldValue::mfint32, "set_normalIndex"),
        NodeInterface(NodeInterface::eventIn, FieldValue::mfint32, "set_texCoordIndex"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "color"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "coord"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "normal"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "texCoord"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "ccw"),
        NodeInterface(NodeInterface::field, FieldValue::mfint32, "colorIndex"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "colorPerVertex"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "convex"),
        NodeInterface(NodeInterface::field, FieldValue::mfint32, "coordIndex"),
        NodeInterface(NodeInterface::field, FieldValue::sffloat, "creaseAngle"),
        NodeInterface(NodeInterface::field, FieldValue::mfint32, "normalIndex"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "normalPerVertex"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "solid"),
        NodeInterface(NodeInterface::field, FieldValue::mfint32, "texCoordIndex")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<IndexedFaceSet>(*this, id));
    Vrml97NodeTypeImpl<IndexedFaceSet> & indexedFaceSetNodeType =
            static_cast<Vrml97NodeTypeImpl<IndexedFaceSet> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<IndexedFaceSet>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            indexedFaceSetNodeType
                    .addEventIn(supportedInterfaces[0].fieldType,
                                supportedInterfaces[0].id,
                                &IndexedFaceSet::processSet_colorIndex);
        } else if (*itr == supportedInterfaces[1]) {
            indexedFaceSetNodeType
                    .addEventIn(supportedInterfaces[1].fieldType,
                                supportedInterfaces[1].id,
                                &IndexedFaceSet::processSet_coordIndex);
        } else if (*itr == supportedInterfaces[2]) {
            indexedFaceSetNodeType
                    .addEventIn(supportedInterfaces[2].fieldType,
                                supportedInterfaces[2].id,
                                &IndexedFaceSet::processSet_normalIndex);
        } else if (*itr == supportedInterfaces[3]) {
            indexedFaceSetNodeType
                    .addEventIn(supportedInterfaces[3].fieldType,
                                supportedInterfaces[3].id,
                                &IndexedFaceSet::processSet_texCoordIndex);
        } else if (*itr == supportedInterfaces[4]) {
            indexedFaceSetNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &IndexedFaceSet::processSet_color,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, SFNode>
                                    (&IndexedFaceSet::color)));
        } else if (*itr == supportedInterfaces[5]) {
            indexedFaceSetNodeType.addExposedField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                &IndexedFaceSet::processSet_coord,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, SFNode>
                                    (&IndexedFaceSet::coord)));
        } else if (*itr == supportedInterfaces[6]) {
            indexedFaceSetNodeType.addExposedField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                &IndexedFaceSet::processSet_normal,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, SFNode>
                                    (&IndexedFaceSet::normal)));
        } else if (*itr == supportedInterfaces[7]) {
            indexedFaceSetNodeType.addExposedField(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                &IndexedFaceSet::processSet_texCoord,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, SFNode>
                                    (&IndexedFaceSet::texCoord)));
        } else if (*itr == supportedInterfaces[8]) {
            indexedFaceSetNodeType.addField(
                supportedInterfaces[8].fieldType,
                supportedInterfaces[8].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, SFBool>
                                    (&IndexedFaceSet::ccw)));
        } else if (*itr == supportedInterfaces[9]) {
            indexedFaceSetNodeType.addField(
                supportedInterfaces[9].fieldType,
                supportedInterfaces[9].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, MFInt32>
                                    (&IndexedFaceSet::colorIndex)));
        } else if (*itr == supportedInterfaces[10]) {
            indexedFaceSetNodeType.addField(
                supportedInterfaces[10].fieldType,
                supportedInterfaces[10].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, SFBool>
                                    (&IndexedFaceSet::colorPerVertex)));
        } else if (*itr == supportedInterfaces[11]) {
            indexedFaceSetNodeType.addField(
                supportedInterfaces[11].fieldType,
                supportedInterfaces[11].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, SFBool>
                                    (&IndexedFaceSet::convex)));
        } else if (*itr == supportedInterfaces[12]) {
            indexedFaceSetNodeType.addField(
                supportedInterfaces[12].fieldType,
                supportedInterfaces[12].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, MFInt32>
                                    (&IndexedFaceSet::coordIndex)));
        } else if (*itr == supportedInterfaces[13]) {
            indexedFaceSetNodeType.addField(
                supportedInterfaces[13].fieldType,
                supportedInterfaces[13].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, SFFloat>
                                    (&IndexedFaceSet::creaseAngle)));
        } else if (*itr == supportedInterfaces[14]) {
            indexedFaceSetNodeType.addField(
                supportedInterfaces[14].fieldType,
                supportedInterfaces[14].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, MFInt32>
                                    (&IndexedFaceSet::normalIndex)));
        } else if (*itr == supportedInterfaces[15]) {
            indexedFaceSetNodeType.addField(
                supportedInterfaces[15].fieldType,
                supportedInterfaces[15].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, SFBool>
                                    (&IndexedFaceSet::normalPerVertex)));
        } else if (*itr == supportedInterfaces[16]) {
            indexedFaceSetNodeType.addField(
                supportedInterfaces[16].fieldType,
                supportedInterfaces[16].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, SFBool>
                                    (&IndexedFaceSet::solid)));
        } else if (*itr == supportedInterfaces[17]) {
            indexedFaceSetNodeType.addField(
                supportedInterfaces[17].fieldType,
                supportedInterfaces[17].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedFaceSet, MFInt32>
                                    (&IndexedFaceSet::texCoordIndex)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class IndexedFaceSet
 *
 * @brief Represents IndexedFaceSet node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node.
 * @param scope     the Scope to which the node belongs.
 */
IndexedFaceSet::IndexedFaceSet(const NodeType & nodeType,
                               const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractIndexedSet(nodeType, scope),
        ccw(true),
        convex(true),
        creaseAngle(0.0),
        normalPerVertex(true),
        solid(true) {
    this->setBVolumeDirty(true);
}

/**
 * @brief Destructor.
 */
IndexedFaceSet::~IndexedFaceSet() throw () {}

/**
 * @brief Determine whether the node has been modified.
 *
 * @return @c true if the node or one of its children has been modified,
 *      @c false otherwise.
 */
bool IndexedFaceSet::isModified() const {
    return (this->d_modified
            || (this->color.value && this->color.value->isModified())
            || (this->coord.value && this->coord.value->isModified())
            || (this->normal.value && this->normal.value->isModified())
            || (this->texCoord.value && this->texCoord.value->isModified()));
}

/**
 * @brief Propagate the bvolume dirty flag from children to parents.
 *
 * @param path  stack of ancestor nodes.
 * @param flags 1 indicates normal modified flag, 2 indicates the
 *              bvolume dirty flag, 3 indicates both.
 */
void IndexedFaceSet::updateModified(NodePath& path, int flags) {
    if (this->isModified()) { markPathModified(path, true, flags); }
    path.push_front(this);
    if (this->color.value) { this->color.value->updateModified(path, flags); }
    if (this->coord.value) { this->coord.value->updateModified(path, flags); }
    if (this->normal.value) { this->normal.value->updateModified(path, flags); }
    if (this->texCoord.value) { this->texCoord.value->updateModified(path, flags); }
    path.pop_front();
}

/**
 * @brief Insert this geometry into @p viewer's display list.
 *
 * @param viewer    a Viewer.
 * @param context   the rendering context.
 *
 * @todo stripify, crease angle, generate normals ...
 */
Viewer::Object IndexedFaceSet::insertGeometry(Viewer & viewer,
                                              const VrmlRenderContext context)
{
    using std::vector;

    if (context.getDrawBSpheres()) {
        const BSphere* bs = (BSphere*)this->getBVolume();
        viewer.drawBSphere(*bs, static_cast<BVolume::Intersection>(4));
    }

    CoordinateNode * const coordinateNode = this->coord.value
                                          ? this->coord.value->toCoordinate()
                                          : 0;
    const vector<vec3f> & coord = coordinateNode
                                ? coordinateNode->getPoint()
                                : vector<vec3f>();

    ColorNode * const colorNode = this->color.value
                                ? this->color.value->toColor()
                                : 0;
    const vector<OpenVRML::color> & color = colorNode
                                          ? colorNode->getColor()
                                          : vector<OpenVRML::color>();

    NormalNode * const normalNode = this->normal.value
                                  ? this->normal.value->toNormal()
                                  : 0;
    const vector<vec3f> & normal = normalNode
                                 ? normalNode->getVector()
                                 : vector<vec3f>();

    TextureCoordinateNode * const texCoordNode = this->texCoord.value
                                ? this->texCoord.value->toTextureCoordinate()
                                : 0;
    const vector<vec2f> & texCoord = texCoordNode
                                   ? texCoordNode->getPoint()
                                   : vector<vec2f>();

    unsigned int optMask = 0;
    if (this->ccw.value) {
        optMask |= Viewer::MASK_CCW;
    }
    if (this->convex.value) {
        optMask |= Viewer::MASK_CONVEX;
    }
    if (this->solid.value) {
        optMask |= Viewer::MASK_SOLID;
    }
    if (this->colorPerVertex.value) {
        optMask |= Viewer::MASK_COLOR_PER_VERTEX;
    }
    if (this->normalPerVertex.value) {
        optMask |= Viewer::MASK_NORMAL_PER_VERTEX;
    }

    const Viewer::Object obj =
            viewer.insertShell(optMask,
                               coord, this->coordIndex.value,
                               color, this->colorIndex.value,
                               normal, this->normalIndex.value,
                               texCoord, this->texCoordIndex.value);

    if (this->color.value)      { this->color.value->clearModified(); }
    if (this->coord.value)      { this->coord.value->clearModified(); }
    if (this->normal.value)     { this->normal.value->clearModified(); }
    if (this->texCoord.value)   { this->texCoord.value->clearModified(); }

    return obj;
}

/**
 * @brief Recalculate the bounding volume.
 */
void IndexedFaceSet::recalcBSphere()
{
    // take the bvolume of all the points. technically, we should figure
    // out just which points are used by the index and just use those,
    // but for a first pass this is fine (also: if we do it this way
    // then we don't have to update the bvolume when the index
    // changes). motto: always do it the simple way first...
    //
    CoordinateNode * const coordinateNode = this->coord.value
                                          ? this->coord.value->toCoordinate()
                                          : 0;
    if (coordinateNode) {
        const std::vector<vec3f> & coord = coordinateNode->getPoint();
        this->bsphere.reset();
        this->bsphere.enclose(&coord[0][0], coord.size());
    }
    this->setBVolumeDirty(false);
}

/**
 * @brief Get the bounding volume.
 *
 * @return the bounding volume associated with the node.
 */
const BVolume * IndexedFaceSet::getBVolume() const {
    if (this->isBVolumeDirty()) { ((IndexedFaceSet*)this)->recalcBSphere(); }
    return &this->bsphere; // hmmm, const?
}

/**
 * @brief set_normal eventIn handler.
 *
 * @param sfnode    an SFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void IndexedFaceSet::processSet_normal(const FieldValue & sfnode,
                                       const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->normal = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("normal_changed", this->normal, timestamp);
}

/**
 * @brief set_normalIndex eventIn handler.
 *
 * @param mfint32   an MFInt32 value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfint32 is not an MFInt32.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void IndexedFaceSet::processSet_normalIndex(const FieldValue & mfint32,
                                            const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->normalIndex = dynamic_cast<const MFInt32 &>(mfint32);
    this->setModified();
}

/**
 * @brief set_texCoord eventIn handler.
 *
 * @param sfnode    an SFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void IndexedFaceSet::processSet_texCoord(const FieldValue & sfnode,
                                         const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->texCoord = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("texCoord_changed", this->texCoord, timestamp);
}

/**
 * @brief set_texCoordIndex eventIn handler.
 *
 * @param mfint32   an MFInt32 value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfint32 is not an MFInt32.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void IndexedFaceSet::processSet_texCoordIndex(const FieldValue & mfint32,
                                              const double timestamp)
         throw (std::bad_cast, std::bad_alloc) {
    this->texCoordIndex = dynamic_cast<const MFInt32 &>(mfint32);
    this->setModified();
}


/**
 * @class IndexedLineSetClass
 *
 * @brief Class object for IndexedLineSet nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this node class object.
 */
IndexedLineSetClass::IndexedLineSetClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
IndexedLineSetClass::~IndexedLineSetClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating IndexedLineSet nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by IndexedLineSetClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        IndexedLineSetClass::createType(const std::string & id,
                                        const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::mfint32, "set_colorIndex"),
        NodeInterface(NodeInterface::eventIn, FieldValue::mfint32, "set_coordIndex"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "color"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "coord"),
        NodeInterface(NodeInterface::field, FieldValue::mfint32, "colorIndex"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "colorPerVertex"),
        NodeInterface(NodeInterface::field, FieldValue::mfint32, "coordIndex")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<IndexedLineSet>(*this, id));
    Vrml97NodeTypeImpl<IndexedLineSet> & indexedLineSetNodeType =
            static_cast<Vrml97NodeTypeImpl<IndexedLineSet> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<IndexedLineSet>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            indexedLineSetNodeType
                    .addEventIn(supportedInterfaces[0].fieldType,
                                supportedInterfaces[0].id,
                                &IndexedLineSet::processSet_colorIndex);
        } else if (*itr == supportedInterfaces[1]) {
            indexedLineSetNodeType
                    .addEventIn(supportedInterfaces[1].fieldType,
                                supportedInterfaces[1].id,
                                &IndexedLineSet::processSet_coordIndex);
        } else if (*itr == supportedInterfaces[2]) {
            indexedLineSetNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &IndexedLineSet::processSet_color,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedLineSet, SFNode>
                                    (&IndexedLineSet::color)));
        } else if (*itr == supportedInterfaces[3]) {
            indexedLineSetNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &IndexedLineSet::processSet_coord,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedLineSet, SFNode>
                                    (&IndexedLineSet::coord)));
        } else if (*itr == supportedInterfaces[4]) {
            indexedLineSetNodeType.addField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedLineSet, MFInt32>
                                    (&IndexedLineSet::colorIndex)));
        } else if (*itr == supportedInterfaces[5]) {
            indexedLineSetNodeType.addField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedLineSet, SFBool>
                                    (&IndexedLineSet::colorPerVertex)));
        } else if (*itr == supportedInterfaces[6]) {
            indexedLineSetNodeType.addField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<IndexedLineSet, MFInt32>
                                    (&IndexedLineSet::coordIndex)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class IndexedLineSet
 *
 * @brief Represents IndexedLineSet node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node.
 * @param scope     the Scope to which the node belongs.
 */
IndexedLineSet::IndexedLineSet(const NodeType & nodeType,
                               const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractIndexedSet(nodeType, scope) {}

/**
 * @brief Destructor.
 */
IndexedLineSet::~IndexedLineSet() throw () {}

/**
 * @brief Insert this geometry into @p viewer's display list.
 *
 * @param viewer    a Viewer.
 * @param context   the rendering context.
 *
 * @todo colors
 */
Viewer::Object IndexedLineSet::insertGeometry(Viewer & viewer,
                                              const VrmlRenderContext context)
{
    using std::vector;

    CoordinateNode * const coordinateNode = this->coord.value
                                          ? this->coord.value->toCoordinate()
                                          : 0;
    const vector<vec3f> & coord = coordinateNode
                                ? coordinateNode->getPoint()
                                : vector<vec3f>();

    ColorNode * const colorNode = this->color.value
                                ? this->color.value->toColor()
                                : 0;
    const vector<OpenVRML::color> & color = colorNode
                                          ? colorNode->getColor()
                                          : vector<OpenVRML::color>();

    Viewer::Object obj = viewer.insertLineSet(coord, this->coordIndex.value,
                                              this->colorPerVertex.value,
                                              color, this->colorIndex.value);

    if (this->color.value) { this->color.value->clearModified(); }
    if (this->coord.value) { this->coord.value->clearModified(); }

    return obj;
}


/**
 * @class InlineClass
 *
 * @brief Class object for Inline nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this node class object.
 */
InlineClass::InlineClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
InlineClass::~InlineClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Inline nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by InlineClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr InlineClass::createType(const std::string & id,
                                          const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "url"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "bboxCenter"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "bboxSize")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Inline>(*this, id));
    Vrml97NodeTypeImpl<Inline> & inlineNodeType =
            static_cast<Vrml97NodeTypeImpl<Inline> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Inline>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            inlineNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &Inline::processSet_url,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Inline, MFString>
                                    (&Inline::url)));
        } else if (*itr == supportedInterfaces[1]) {
            inlineNodeType.addField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Inline, SFVec3f>
                                    (&Inline::bboxCenter)));
        } else if (*itr == supportedInterfaces[2]) {
            inlineNodeType.addField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Inline, SFVec3f>
                                    (&Inline::bboxSize)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Inline
 *
 * @brief Represents Inline node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with this node.
 * @param scope     the Scope to which the node belongs.
 */
Inline::Inline(const NodeType & nodeType,
               const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    ChildNode(nodeType, scope),
    GroupingNode(nodeType, scope),
    inlineScene(0),
    hasLoaded(false)
{
    this->setBVolumeDirty(true);
}

/**
 * @brief Destructor.
 */
Inline::~Inline() throw () {}

/**
 * @brief Render the node.
 *
 * Render each of the children.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void Inline::render(Viewer & viewer, const VrmlRenderContext context)
{
    this->load();
    if (this->inlineScene) { this->inlineScene->render(viewer, context); }
}

Inline * Inline::toInline() const { return const_cast<Inline *>(this); }

/**
 * @brief Get the children in the scene graph.
 *
 * @return the child nodes in the scene graph.
 */
const std::vector<NodePtr> & Inline::getChildren() const throw ()
{
    static const std::vector<NodePtr> empty;
    return this->inlineScene
            ? this->inlineScene->getNodes()
            : empty;
}

/**
 * Pass on to enabled touchsensor child.
 */
void Inline::activate(double time, bool isOver, bool isActive, double *p)
{
    const std::vector<NodePtr> & children = this->getChildren();
    for (size_t i = 0; i < children.size(); ++i) {
        const NodePtr & node = children[i];
        if (node) {
            if (node->toTouchSensor() && node->toTouchSensor()->isEnabled()) {
                node->toTouchSensor()->activate(time, isOver, isActive, p);
            } else if (node->toPlaneSensor()
                    && node->toPlaneSensor()->isEnabled()) {
                node->toPlaneSensor()->activate(time, isActive, p);
            } else if (node->toCylinderSensor()
                    && node->toCylinderSensor()->isEnabled()) {
                node->toCylinderSensor()->activate(time, isActive, p);
            } else if (node->toSphereSensor()
                    && node->toSphereSensor()->isEnabled()) {
                node->toSphereSensor()->activate(time, isActive, p);
            }
        }
    }
}

/**
 * @brief Load the children from the URL.
 */
void Inline::load() {
    //
    // XXX Need to check whether Url has been modified.
    //
    if (this->hasLoaded) { return; }

    this->hasLoaded = true; // although perhaps not successfully
    this->setBVolumeDirty(true);

    assert(this->getScene());
    this->inlineScene = new Scene(this->getScene()->browser,
                                  this->url.value,
                                  this->getScene());
    this->inlineScene->initialize(Browser::getCurrentTime());
}

/**
 * @brief set_url eventIn handler.
 *
 * @param mfstring  an MFString value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 *
 * @todo Currently this only changes the field value; it does not load a new
 *      Scene.
 */
void Inline::processSet_url(const FieldValue & mfstring, const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->url = dynamic_cast<const MFString &>(mfstring);
    this->setModified();
    this->emitEvent("url_changed", this->url, timestamp);
}


/**
 * @class LODClass
 *
 * @brief Class object for LOD nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this node class object.
 */
LODClass::LODClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
LODClass::~LODClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating LOD nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by LODClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr LODClass::createType(const std::string & id,
                                       const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::mfnode, "level"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "center"),
        NodeInterface(NodeInterface::field, FieldValue::mffloat, "range")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<LOD>(*this, id));
    Vrml97NodeTypeImpl<LOD> & lodNodeType =
            static_cast<Vrml97NodeTypeImpl<LOD> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<LOD>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            lodNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &LOD::processSet_level,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<LOD, MFNode>
                                    (&LOD::level)));
        } else if (*itr == supportedInterfaces[1]) {
            lodNodeType.addField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<LOD, SFVec3f>
                                    (&LOD::center)));
        } else if (*itr == supportedInterfaces[2]) {
            lodNodeType.addField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<LOD, MFFloat>
                                    (&LOD::range)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class LOD
 *
 * @brief Represents LOD node instances.
 */

/**
 * @var BSphere LOD::bsphere
 *
 * @brief Cached copy of the BSphere enclosing this node's children.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with this node.
 * @param scope     the Scope to which the node belongs.
 */
LOD::LOD(const NodeType & nodeType,
         const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    ChildNode(nodeType, scope),
    GroupingNode(nodeType, scope),
    children(1)
{
    this->setBVolumeDirty(true); // lazy calc of bvolume
}

/**
 * @brief Destructor.
 */
LOD::~LOD() throw () {}

/**
 * @brief Determine whether the node has been modified.
 *
 * @return @c true if the node or one of its children has been modified,
 *      @c false otherwise.
 */
bool LOD::isModified() const {
    if (this->d_modified) { return true; }

    // This should really check which range is being rendered...
    for (size_t i = 0; i < this->level.value.size(); ++i) {
        if (this->level.value[i]->isModified()) { return true; }
    }
    return false;
}

/**
 * @brief Propagate the bvolume dirty flag from children to parents.
 *
 * @param path  stack of ancestor nodes.
 * @param flags 1 indicates normal modified flag, 2 indicates the
 *              bvolume dirty flag, 3 indicates both.
 */
void LOD::updateModified(NodePath & path, int flags) {
    //
    // what happens if one of the other children suddenly becomes the one
    // selected? to be safe: check them all. this potentially means some
    // extra work, but it's a lot easier to reason about.
    //
    if (this->isModified()) { markPathModified(path, true); }
    path.push_front(this);
    for (size_t i = 0; i < this->level.value.size(); ++i) {
        this->level.value[i]->updateModified(path);
    }
    path.pop_front();
}

/**
 * @brief Render the node.
 *
 * Render one of the children.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void LOD::render(Viewer & viewer, const VrmlRenderContext context)
{
    this->clearModified();
    if (this->level.value.size() <= 0) { return; }

    float x, y, z;

    mat4f MV = context.getMatrix();
    MV = MV.inverse();
    x = MV[3][0]; y = MV[3][1]; z = MV[3][2];
    float dx = x - this->center.value.x();
    float dy = y - this->center.value.y();
    float dz = z - this->center.value.z();
    float d2 = dx * dx + dy * dy + dz * dz;

    size_t i;
    for (i = 0; i < this->range.value.size(); ++i) {
        if (d2 < this->range.value[i] * this->range.value[i]) {
            break;
        }
    }

    // Should choose an "optimal" level...
    if (this->range.value.size() == 0) { i = this->level.value.size() - 1; }

    // Not enough levels...
    if (i >= this->level.value.size()) { i = this->level.value.size() - 1; }

    this->level.value[i]->render(viewer, context);

    // Don't re-render on their accounts
    for (i = 0; i < this->level.value.size(); ++i) {
        this->level.value[i]->clearModified();
    }
}

/**
 * @brief Get the bounding volume.
 *
 * @return the bounding volume associated with the node.
 */
const BVolume * LOD::getBVolume() const {
    if (this->isBVolumeDirty()) {
        ((LOD*)this)->recalcBSphere();
    }
    return &this->bsphere;
}

/**
 * @brief Get the children in the scene graph.
 *
 * @return the child nodes in the scene graph.
 */
const std::vector<NodePtr> & LOD::getChildren() const throw ()
{
    return this->children.value;
}

/**
 * Pass on to enabled touchsensor child.
 */
void LOD::activate(double time, bool isOver, bool isActive, double *p)
{
    const std::vector<NodePtr> & children = this->getChildren();
    const NodePtr & node = children[0];
    if (node) {
        if (node->toTouchSensor() && node->toTouchSensor()->isEnabled()) {
            node->toTouchSensor()->activate(time, isOver, isActive, p);
        } else if (node->toPlaneSensor()
                && node->toPlaneSensor()->isEnabled()) {
            node->toPlaneSensor()->activate(time, isActive, p);
        } else if (node->toCylinderSensor()
                && node->toCylinderSensor()->isEnabled()) {
            node->toCylinderSensor()->activate(time, isActive, p);
        } else if (node->toSphereSensor()
                && node->toSphereSensor()->isEnabled()) {
            node->toSphereSensor()->activate(time, isActive, p);
        }
    }
}

/**
 * @brief Recalculate the bounding volume.
 */
void LOD::recalcBSphere() {
    this->bsphere.reset();

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
    for (size_t i = 0; i < this->level.value.size(); i++) {
        const NodePtr & node = this->level.value[i];
        if (node) {
            const BVolume * ci_bv = node->getBVolume();
            this->bsphere.extend(*ci_bv);
        }
    }
    this->setBVolumeDirty(false);
}

/**
 * @brief set_level eventIn handler.
 *
 * @param mfnode    an MFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfnode is not an MFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void LOD::processSet_level(const FieldValue & mfnode, const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->level = dynamic_cast<const MFNode &>(mfnode);
    this->setModified();
    this->emitEvent("level_changed", this->level, timestamp);
}


/**
 * @class MaterialClass
 *
 * @brief Class object for Material nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this node class object.
 */
MaterialClass::MaterialClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
MaterialClass::~MaterialClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Material nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by MaterialClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr MaterialClass::createType(const std::string & id,
                                            const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "ambientIntensity"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfcolor, "diffuseColor"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfcolor, "emissiveColor"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "shininess"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfcolor, "specularColor"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "transparency")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Material>(*this, id));
    Vrml97NodeTypeImpl<Material> & materialNodeType =
            static_cast<Vrml97NodeTypeImpl<Material> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Material>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            materialNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &Material::processSet_ambientIntensity,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Material, SFFloat>
                                    (&Material::ambientIntensity)));
        } else if (*itr == supportedInterfaces[1]) {
            materialNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &Material::processSet_diffuseColor,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Material, SFColor>
                                    (&Material::diffuseColor)));
        } else if (*itr == supportedInterfaces[2]) {
            materialNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &Material::processSet_emissiveColor,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Material, SFColor>
                                    (&Material::emissiveColor)));
        } else if (*itr == supportedInterfaces[3]) {
            materialNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &Material::processSet_shininess,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Material, SFFloat>
                                    (&Material::shininess)));
        } else if (*itr == supportedInterfaces[4]) {
            materialNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &Material::processSet_specularColor,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Material, SFColor>
                                    (&Material::specularColor)));
        } else if (*itr == supportedInterfaces[5]) {
            materialNodeType.addExposedField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                &Material::processSet_transparency,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Material, SFFloat>
                                    (&Material::transparency)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Material
 *
 * @brief Material node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with this node.
 * @param scope     the Scope to which the node belongs.
 */
Material::Material(const NodeType & nodeType,
                   const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    MaterialNode(nodeType, scope),
    ambientIntensity(0.2),
    diffuseColor(color(0.8, 0.8, 0.8)),
    emissiveColor(color(0.0, 0.0, 0.0)),
    shininess(0.2),
    specularColor(color(0.0, 0.0, 0.0)),
    transparency(0.0)
{}

/**
 * @brief Destructor.
 */
Material::~Material() throw () {}

/**
 * @brief set_ambientIntensity eventIn handler.
 *
 * @param sffloat   a value from 0.0 to 1.0.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 */
void Material::processSet_ambientIntensity(const FieldValue & sffloat,
                                           const double timestamp)
        throw (std::bad_cast) {
    this->ambientIntensity = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("ambientIntensity_changed", this->ambientIntensity,
                    timestamp);
}

/**
 * @brief set_diffuseColor eventIn handler.
 *
 * @param sfcolor   an SFColor value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfcolor is not an SFColor.
 */
void Material::processSet_diffuseColor(const FieldValue & sfcolor,
                                       const double timestamp)
        throw (std::bad_cast) {
    this->diffuseColor = dynamic_cast<const SFColor &>(sfcolor);
    this->setModified();
    this->emitEvent("diffuseColor_changed", this->diffuseColor, timestamp);
}

/**
 * @brief set_emissiveColor eventIn handler.
 *
 * @param sfcolor   an SFColor value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfcolor is not an SFColor.
 */
void Material::processSet_emissiveColor(const FieldValue & sfcolor,
                                        const double timestamp)
        throw (std::bad_cast) {
    this->emissiveColor = dynamic_cast<const SFColor &>(sfcolor);
    this->setModified();
    this->emitEvent("emissiveColor_changed", this->emissiveColor, timestamp);
}

/**
 * @brief set_shininess eventIn handler.
 *
 * @param sffloat   a value from 0.0 to 1.0.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 */
void Material::processSet_shininess(const FieldValue & sffloat,
                                    const double timestamp)
        throw (std::bad_cast) {
    this->shininess = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("shininess_changed", this->shininess, timestamp);
}

/**
 * @brief set_specularColor eventIn handler.
 *
 * @param sfcolor   an SFColor value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfcolor is not an SFColor.
 */
void Material::processSet_specularColor(const FieldValue & sfcolor,
                                        const double timestamp)
        throw (std::bad_cast) {
    this->specularColor = dynamic_cast<const SFColor &>(sfcolor);
    this->setModified();
    this->emitEvent("specularColor_changed", this->specularColor, timestamp);
}

/**
 * @brief set_transparency eventIn handler.
 *
 * @param sffloat   a value from 0.0 to 1.0.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 */
void Material::processSet_transparency(const FieldValue & sffloat,
                                       const double timestamp)
        throw (std::bad_cast) {
    this->transparency = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("transparency_changed", this->transparency, timestamp);
}

/**
 * @brief Get the ambient intensity.
 *
 * @return the ambient intensity.
 */
float Material::getAmbientIntensity() const throw ()
{
    return this->ambientIntensity.value;
}

/**
 * @brief Get the diffuse color.
 *
 * @return the diffuse color.
 */
const color & Material::getDiffuseColor() const throw ()
{
    return this->diffuseColor.value;
}

/**
 * @brief Get the emissive color.
 *
 * @return the emissive color.
 */
const color & Material::getEmissiveColor() const throw ()
{
    return this->emissiveColor.value;
}

/**
 * @brief Get the shininess.
 *
 * @return the shininess.
 */
float Material::getShininess() const throw ()
{
    return this->shininess.value;
}

/**
 * @brief Get the specular color.
 *
 * @return the specular color.
 */
const color & Material::getSpecularColor() const throw ()
{
    return this->specularColor.value;
}

/**
 * @brief Get the transparency.
 *
 * @return the transparency.
 */
float Material::getTransparency() const throw () 
{
    return this->transparency.value;
}


/**
 * @class MovieTextureClass
 *
 * @brief Class object for MovieTexture nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
MovieTextureClass::MovieTextureClass(Browser & browser):
        NodeClass(browser) {}

/**
 * @brief Destructor.
 */
MovieTextureClass::~MovieTextureClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating MovieTexture nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by MovieTextureClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        MovieTextureClass::createType(const std::string & id,
                                      const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "loop"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "speed"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sftime, "startTime"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sftime, "stopTime"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "url"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "repeatS"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "repeatT"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sftime, "duration_changed"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isActive")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<MovieTexture>(*this, id));
    Vrml97NodeTypeImpl<MovieTexture> & movieTextureNodeType =
            static_cast<Vrml97NodeTypeImpl<MovieTexture> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<MovieTexture>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            movieTextureNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &MovieTexture::processSet_loop,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<MovieTexture, SFBool>
                                    (&MovieTexture::loop)));
        } else if (*itr == supportedInterfaces[1]) {
            movieTextureNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &MovieTexture::processSet_speed,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<MovieTexture, SFFloat>
                                    (&MovieTexture::speed)));
        } else if (*itr == supportedInterfaces[2]) {
            movieTextureNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &MovieTexture::processSet_startTime,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<MovieTexture, SFTime>
                                    (&MovieTexture::startTime)));
        } else if (*itr == supportedInterfaces[3]) {
            movieTextureNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &MovieTexture::processSet_stopTime,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<MovieTexture, SFTime>
                                    (&MovieTexture::stopTime)));
        } else if (*itr == supportedInterfaces[4]) {
            movieTextureNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &MovieTexture::processSet_url,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<MovieTexture, MFString>
                                    (&MovieTexture::url)));
        } else if (*itr == supportedInterfaces[5]) {
            movieTextureNodeType.addField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<MovieTexture, SFBool>
                                    (&MovieTexture::repeatS)));
        } else if (*itr == supportedInterfaces[6]) {
            movieTextureNodeType.addField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<MovieTexture, SFBool>
                                    (&MovieTexture::repeatT)));
        } else if (*itr == supportedInterfaces[7]) {
            movieTextureNodeType.addEventOut(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<MovieTexture, SFTime>
                                    (&MovieTexture::duration)));
        } else if (*itr == supportedInterfaces[8]) {
            movieTextureNodeType.addEventOut(
                supportedInterfaces[8].fieldType,
                supportedInterfaces[8].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<MovieTexture, SFBool>
                                    (&MovieTexture::active)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class MovieTexture
 *
 * @brief MovieTexture node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
MovieTexture::MovieTexture(const NodeType & nodeType,
                           const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractTexture(nodeType, scope),
        loop(false),
        speed(1.0),
        image(0),
        frame(0),
        lastFrame(-1),
        lastFrameTime(-1.0),
        texObject(0) {}

/**
 * @brief Destructor.
 */
MovieTexture::~MovieTexture() throw ()
{
    delete this->image;
}

MovieTexture* MovieTexture::toMovieTexture() const
{ return (MovieTexture*) this; }

void MovieTexture::update(const double currentTime)
{
    if (isModified()) {
        if (this->image) {
            const char * imageUrl = this->image->url();
            size_t imageLen = strlen(imageUrl);
            size_t i, nUrls = this->url.value.size();
            for (i = 0; i < nUrls; ++i) {
                size_t len = this->url.value[i].length();

                if (this->url.value[i] == imageUrl
                        || (imageLen > len
                            && this->url.value[i]
                                == (imageUrl + imageLen - len))) {
                    break;
                }
            }

            // if (d_image->url() not in d_url list) ...
            if (i == nUrls) {
                delete this->image;
                this->image = 0;
            }
        }
    }

    // Load the movie if needed (should check startTime...)
    if (!this->image && this->url.value.size() > 0) {
        Doc2 baseDoc(this->getScene()->getURI());
        this->image = new Image;
        if (!this->image->tryURLs(this->url, &baseDoc)) {
            std::cerr << "Error: couldn't read MovieTexture from URL "
                      << this->url << std::endl;
        }

        int nFrames = this->image->nFrames();
        this->duration = SFTime((nFrames >= 0) ? double(nFrames) : double(-1));
        this->emitEvent("duration_changed", this->duration, currentTime);
        this->frame = (this->speed.value >= 0) ? 0 : nFrames-1;
        // Set the last frame equal to the start time.
        // This is needed to properly handle the case where the startTime
        // and stopTime are set at runtime to the same value (spec says
        // that a single loop should occur in this case...)
        this->lastFrameTime = this->startTime.value;
    }

    // No pictures to show
    if (!this->image || this->image->nFrames() == 0) { return; }

    // See section 4.6.9 of the VRML97 spec for a detailed explanation
    // of the logic here.
    if (!this->active.value) {
        if (currentTime >= this->startTime.value) {
            if (currentTime >= this->stopTime.value) {
                if (this->startTime.value >= this->stopTime.value) {
                    if (this->loop.value) {
                        this->active.value = true;
                        this->emitEvent("isActive", this->active, currentTime);
                        this->lastFrameTime = currentTime;
                        this->frame = (this->speed.value >= 0) ? 0 :
                                         this->image->nFrames() - 1;
                        setModified();
	            } else if (this->startTime.value > this->lastFrameTime) {
                        this->active.value = true;
                        this->emitEvent("isActive", this->active, currentTime);
                        this->lastFrameTime = currentTime;
                        this->frame = (this->speed.value >= 0) ? 0 :
                                         this->image->nFrames() - 1;
                        setModified();
	            }
	        }
            } else if (this->stopTime.value > currentTime) {
                this->active.value = true;
                this->emitEvent("isActive", this->active, currentTime);
                this->lastFrameTime = currentTime;
                this->frame = (this->speed.value >= 0) ? 0 :
                                 this->image->nFrames() - 1;
                setModified();
            }
        }
    }
    // Check whether stopTime has passed
    else if (this->active.value
             && ((this->stopTime.value > this->startTime.value
		  && this->stopTime.value <= currentTime))
             || ((this->frame < 0) && !this->loop.value)) {
        this->active.value = false;
        this->emitEvent("isActive", this->active, currentTime);
        setModified();
    } else if (this->frame < 0 && this->loop.value) {
        // Reset frame to 0 to begin loop again.
        this->frame = 0;
    }

    // Check whether the frame should be advanced
    else if (this->active.value
             && this->lastFrameTime + fabs(1 / this->speed.value)
                <= currentTime) {
        if (this->speed.value < 0.0) {
            --this->frame;
        } else {
            ++this->frame;
        }

        this->lastFrameTime = currentTime;
        setModified();
    }

    // Tell the scene when the next update is needed.
    if (this->active.value) {
        double d = this->lastFrameTime + fabs(1 / this->speed.value)
                    - currentTime;
        this->nodeType.nodeClass.browser.setDelta(0.9 * d);
    }
}

/**
 * @brief Render the node.
 *
 * Render a frame if there is one available.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void MovieTexture::render(Viewer & viewer, const VrmlRenderContext context)
{
    if (!this->image || this->frame < 0) { return; }

    unsigned char * pix = this->image->pixels(this->frame);

    if (this->frame != this->lastFrame && this->texObject) {
        viewer.removeTextureObject(this->texObject);
        this->texObject = 0;
    }

    if (!pix) {
        this->frame = -1;
    } else if (this->texObject) {
        viewer.insertTextureReference(this->texObject, this->image->nc());
    } else {
        // Ensure image dimensions are powers of 2 (move to NodeTexture...)
        int sizes[] = { 2, 4, 8, 16, 32, 64, 128, 256 };
        int nSizes = sizeof(sizes) / sizeof(int);
        int w = this->image->w();
        int h = this->image->h();
        int i, j;
        for (i = 0; i < nSizes; ++i) { if (w < sizes[i]) { break; } }
        for (j = 0; j < nSizes; ++j) { if (h < sizes[j]) { break; } }

        if (i > 0 && j > 0) {
            // Always scale images down in size and reuse the same pixel memory.
            if (w != sizes[i - 1] || h != sizes[j - 1]) {
                viewer.scaleTexture(w, h, sizes[i - 1], sizes[j - 1],
                                    this->image->nc(), pix);
                this->image->setSize(sizes[i - 1], sizes[j - 1]);
            }

            this->texObject = viewer.insertTexture(this->image->w(),
                                                   this->image->h(),
                                                   this->image->nc(),
                                                   this->repeatS.value,
                                                   this->repeatT.value,
                                                   pix,
                                                   !this->active.value);
        }
    }

    this->lastFrame = this->frame;
    this->clearModified();
}

size_t MovieTexture::nComponents() const throw () {
    return this->image ? this->image->nc() : 0;
}

size_t MovieTexture::width() const throw () {
    return this->image ? this->image->w() : 0;
}

size_t MovieTexture::height() const throw () {
    return this->image ? this->image->h() : 0;
}

size_t MovieTexture::nFrames() const throw () {
    return this->image ? this->image->nFrames() : 0;
}

const unsigned char * MovieTexture::pixels() const throw () {
    return this->image ? this->image->pixels() : 0;
}

/**
 * @brief Initialize.
 *
 * @param timestamp the current time.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void MovieTexture::do_initialize(const double timestamp) throw (std::bad_alloc)
{
    assert(this->getScene());
    this->getScene()->browser.addMovie(*this);
}

/**
 * @brief Shut down.
 *
 * @param timestamp the current time.
 */
void MovieTexture::do_shutdown(const double timestamp) throw ()
{
    assert(this->getScene());
    this->getScene()->browser.removeMovie(*this);
}

/**
 * @brief set_loop eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void MovieTexture::processSet_loop(const FieldValue & sfbool,
                                   const double timestamp)
        throw (std::bad_cast) {
    this->loop = dynamic_cast<const SFBool &>(sfbool);
    this->setModified();
    this->emitEvent("loop_changed", this->loop, timestamp);
}

/**
 * @brief set_speed eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void MovieTexture::processSet_speed(const FieldValue & sffloat,
                                    const double timestamp)
        throw (std::bad_cast) {
    //
    // set_speed is ignored if the MovieTexture is active.
    //
    if (!this->active.value) {
        this->speed = dynamic_cast<const SFFloat &>(sffloat);
        this->setModified();
        this->emitEvent("speed_changed", this->speed, timestamp);
    }
}

/**
 * @brief set_startTime eventIn handler.
 *
 * @param sftime    an SFTime value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sftime is not an SFTime.
 */
void MovieTexture::processSet_startTime(const FieldValue & sftime,
                                        const double timestamp)
        throw (std::bad_cast) {
    this->startTime = dynamic_cast<const SFTime &>(sftime);
    this->setModified();
    this->emitEvent("startTime_changed", this->startTime, timestamp);
}

/**
 * @brief set_stopTime eventIn handler.
 *
 * @param sftime    an SFTime value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sftime is not an SFTime.
 */
void MovieTexture::processSet_stopTime(const FieldValue & sftime,
                                       const double timestamp)
        throw (std::bad_cast) {
    this->stopTime = dynamic_cast<const SFTime &>(sftime);
    this->setModified();
    this->emitEvent("stopTime_changed", this->stopTime, timestamp);
}

/**
 * @brief set_url eventIn handler.
 *
 * @param mfstring  an MFString value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void MovieTexture::processSet_url(const FieldValue & mfstring,
                                  const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->url = dynamic_cast<const MFString &>(mfstring);
    this->setModified();
    this->emitEvent("url_changed", this->url, timestamp);
}


/**
 * @class NavigationInfoClass
 *
 * @brief Class object for NavigationInfo nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
NavigationInfoClass::NavigationInfoClass(Browser & browser):
        NodeClass(browser) {}

/**
 * @brief Destructor.
 */
NavigationInfoClass::~NavigationInfoClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating NavigationInfo nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by NavigationInfoClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        NavigationInfoClass::createType(const std::string & id,
                                        const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::sfbool, "set_bind"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mffloat, "avatarSize"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "headlight"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "speed"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "type"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "visibilityLimit"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isBound")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<NavigationInfo>(*this, id));
    Vrml97NodeTypeImpl<NavigationInfo> & navigationInfoNodeType =
            static_cast<Vrml97NodeTypeImpl<NavigationInfo> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<NavigationInfo>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            navigationInfoNodeType.addEventIn(supportedInterfaces[0].fieldType,
                                   supportedInterfaces[0].id,
                                   &NavigationInfo::processSet_bind);
        } else if (*itr == supportedInterfaces[1]) {
            navigationInfoNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &NavigationInfo::processSet_avatarSize,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<NavigationInfo, MFFloat>
                                    (&NavigationInfo::avatarSize)));
        } else if (*itr == supportedInterfaces[2]) {
            navigationInfoNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &NavigationInfo::processSet_headlight,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<NavigationInfo, SFBool>
                                    (&NavigationInfo::headlight)));
        } else if (*itr == supportedInterfaces[3]) {
            navigationInfoNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &NavigationInfo::processSet_speed,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<NavigationInfo, SFFloat>
                                    (&NavigationInfo::speed)));
        } else if (*itr == supportedInterfaces[4]) {
            navigationInfoNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &NavigationInfo::processSet_type,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<NavigationInfo, MFString>
                                    (&NavigationInfo::type)));
        } else if (*itr == supportedInterfaces[5]) {
            navigationInfoNodeType.addExposedField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                &NavigationInfo::processSet_visibilityLimit,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<NavigationInfo, SFFloat>
                                    (&NavigationInfo::visibilityLimit)));
        } else if (*itr == supportedInterfaces[6]) {
            navigationInfoNodeType.addEventOut(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<NavigationInfo, SFBool>
                                    (&NavigationInfo::bound)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

namespace {
    const float avatarSize_[] = { 0.25, 1.6, 0.75 };
    const std::string type_[] = { "WALK", "ANY" };
}

/**
 * @class NavigationInfo
 *
 * @brief NavigationInfo node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
NavigationInfo::NavigationInfo(const NodeType & nodeType,
                               const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope),
    avatarSize(avatarSize_, avatarSize_ + 3),
    headlight(true),
    speed(1.0),
    type(type_, type_ + 2),
    visibilityLimit(0.0),
    bound(false)
{}

/**
 * @brief Destructor.
 */
NavigationInfo::~NavigationInfo() throw ()
{}

NavigationInfo* NavigationInfo::toNavigationInfo() const
{ return (NavigationInfo*) this; }

/**
 * @brief Initialize.
 *
 * @param timestamp the current time.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void NavigationInfo::do_initialize(const double timestamp)
    throw (std::bad_alloc)
{
    assert(this->getScene());
    this->getScene()->browser.addNavigationInfo(*this);
}

/**
 * @brief Shut down.
 *
 * @param timestamp the current time.
 */
void NavigationInfo::do_shutdown(const double timestamp) throw ()
{
    assert(this->getScene());
    this->getScene()->browser.removeNavigationInfo(*this);
}

/**
 * @brief set_avatarSize eventIn handler.
 *
 * @param mffloat   an MFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mffloat is not an MFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void NavigationInfo::processSet_avatarSize(const FieldValue & mffloat,
                                           const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->avatarSize = dynamic_cast<const MFFloat &>(mffloat);
    this->setModified();
    this->emitEvent("avatarSize_changed", this->avatarSize, timestamp);
}

/**
 * @brief set_bind eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfbool is not an SFBool.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void NavigationInfo::processSet_bind(const FieldValue & sfbool,
                                     const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    NavigationInfo * current =
            this->nodeType.nodeClass.browser.bindableNavigationInfoTop();
    const SFBool & b = dynamic_cast<const SFBool &>(sfbool);

    if (b.value) {        // set_bind TRUE
        if (this != current) {
            if (current) {
                current->bound.value = false;
                current->emitEvent("isBound", current->bound, timestamp);
            }
            this->nodeType.nodeClass.browser.bindablePush(this);
            this->bound.value = true;
            this->emitEvent("isBound", this->bound, timestamp);
        }
    } else {            // set_bind FALSE
        this->nodeType.nodeClass.browser.bindableRemove(this);
        if (this == current) {
            this->bound.value = false;
            this->emitEvent("isBound", this->bound, timestamp);
            current = this->nodeType.nodeClass.browser
                        .bindableNavigationInfoTop();
            if (current) {
                current->bound.value = true;
                current->emitEvent("isBound", current->bound, timestamp);
            }
        }
    }
}

/**
 * @brief set_headlight eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfbool is not an SFBool.
 */
void NavigationInfo::processSet_headlight(const FieldValue & sfbool,
                                          const double timestamp)
        throw (std::bad_cast) {
    this->headlight = dynamic_cast<const SFBool &>(sfbool);
    this->setModified();
    this->emitEvent("headlight_changed", this->headlight, timestamp);
}

/**
 * @brief set_speed eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 */
void NavigationInfo::processSet_speed(const FieldValue & sffloat,
                                      const double timestamp)
        throw (std::bad_cast) {
    this->speed = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("speed_changed", this->speed, timestamp);
}

/**
 * @brief set_type eventIn handler.
 *
 * @param mfstring  an MFString value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void NavigationInfo::processSet_type(const FieldValue & mfstring,
                                     const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->type = dynamic_cast<const MFString &>(mfstring);
    this->setModified();
    this->emitEvent("type_changed", this->type, timestamp);
}

/**
 * @brief set_visibilityLimit eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 */
void NavigationInfo::processSet_visibilityLimit(const FieldValue & sffloat,
                                                const double timestamp)
        throw (std::bad_cast) {
    this->visibilityLimit = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("visibilityLimit_changed", this->visibilityLimit,
                    timestamp);
}


/**
 * @class NormalClass
 *
 * @brief Class object for Material nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this node class object.
 */
NormalClass::NormalClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
NormalClass::~NormalClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Normal nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by NormalClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr NormalClass::createType(const std::string & id,
                                          const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterface =
            NodeInterface(NodeInterface::exposedField, FieldValue::mfvec3f, "vector");
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Normal>(*this, id));
    Vrml97NodeTypeImpl<Normal> & normalNodeType =
            static_cast<Vrml97NodeTypeImpl<Normal> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Normal>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterface) {
            normalNodeType.addExposedField(
                supportedInterface.fieldType,
                supportedInterface.id,
                &Normal::processSet_vector,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Normal, MFVec3f>
                                    (&Normal::vector)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Normal
 *
 * @brief Normal node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node.
 * @param scope     the Scope to which the node belongs.
 */
Normal::Normal(const NodeType & nodeType,
               const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractBase(nodeType, scope),
        NormalNode(nodeType, scope) {}

/**
 * @brief Destructor.
 */
Normal::~Normal() throw () {}

/**
 * @brief Get the array of normal vectors.
 *
 * @return the array of normal vectors.
 */
const std::vector<vec3f> & Normal::getVector() const throw ()
{
    return this->vector.value;
}

/**
 * @brief set_vector eventIn handler.
 *
 * @param mfvec3f   an MFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfvec3f is not an MFVec3f.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Normal::processSet_vector(const FieldValue & mfvec3f,
                               const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->vector = dynamic_cast<const MFVec3f &>(mfvec3f);
    this->setModified();
    this->emitEvent("vector_changed", this->vector, timestamp);
}


/**
 * @class NormalInterpolatorClass
 *
 * @brief Class object for NormalInterpolator nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
NormalInterpolatorClass::NormalInterpolatorClass(Browser & browser):
        NodeClass(browser) {}

/**
 * @brief Destructor.
 */
NormalInterpolatorClass::~NormalInterpolatorClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating NormalInterpolator
 *      nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by NormalInterpolatorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        NormalInterpolatorClass::createType(const std::string & id,
                                            const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::sffloat, "set_fraction"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mffloat, "key"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfvec3f, "keyValue"),
        NodeInterface(NodeInterface::eventOut, FieldValue::mfvec3f, "value_changed")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<NormalInterpolator>(*this, id));
    Vrml97NodeTypeImpl<NormalInterpolator> & normalInterpolatorNodeType =
            static_cast<Vrml97NodeTypeImpl<NormalInterpolator> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<NormalInterpolator>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            normalInterpolatorNodeType
                    .addEventIn(supportedInterfaces[0].fieldType,
                                supportedInterfaces[0].id,
                                &NormalInterpolator::processSet_fraction);
        } else if (*itr == supportedInterfaces[1]) {
            normalInterpolatorNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &NormalInterpolator::processSet_key,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<NormalInterpolator, MFFloat>
                                    (&NormalInterpolator::key)));
        } else if (*itr == supportedInterfaces[2]) {
            normalInterpolatorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &NormalInterpolator::processSet_keyValue,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<NormalInterpolator, MFVec3f>
                                    (&NormalInterpolator::keyValue)));
        } else if (*itr == supportedInterfaces[3]) {
            normalInterpolatorNodeType.addEventOut(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<NormalInterpolator, MFVec3f>
                                    (&NormalInterpolator::value)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class NormalInterpolator
 *
 * @brief NormalInterpolator node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
NormalInterpolator::NormalInterpolator(const NodeType & nodeType,
                                       const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractChild(nodeType, scope) {}

/**
 * @brief Destructor.
 */
NormalInterpolator::~NormalInterpolator() throw () {}

/**
 * @brief set_fraction eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void NormalInterpolator::processSet_fraction(const FieldValue & sffloat,
                                             const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    using OpenVRML_::fptolerance;

    float f = dynamic_cast<const SFFloat &>(sffloat).value;

    size_t nNormals = this->keyValue.value.size() / this->key.value.size();
    size_t n = this->key.value.size() - 1;

    if (f < this->key.value[0]) {
        this->value.value.assign(this->keyValue.value.begin(),
                                 this->keyValue.value.begin() + nNormals);
    } else if (f > this->key.value[n]) {
        this->value.value
                .assign(this->keyValue.value.begin() + n * nNormals,
                        this->keyValue.value.begin() + (n + 1) * nNormals);
    } else {
        // Reserve enough space for the new value
        this->value.value.resize(nNormals);

        for (size_t i = 0; i < n; ++i) {
            if (this->key.value[i] <= f && f <= this->key.value[i + 1]) {
                std::vector<vec3f>::const_iterator v1 =
                        this->keyValue.value.begin() + i * nNormals;
                std::vector<vec3f>::const_iterator v2 =
                        this->keyValue.value.begin() + (i + 1) * nNormals;

                f = (f - this->key.value[i])
                    / (this->key.value[i + 1] - this->key.value[i]);

                // Interpolate on the surface of unit sphere.
                // Contributed by S. K. Bose. (bose@garuda.barc.ernet.in)
                for (int j = 0; j < nNormals; ++j) {
                    float alpha, beta;
                    float dotval = v1->dot(*v2);
                    if ((dotval + 1.0) > fptolerance) { // Vectors are not opposite
                        if ((1.0-dotval) > fptolerance) { // Vectors are not coincide
                            float omega = acos(dotval);
                            float sinomega = sin(omega);
                            alpha = sin((1.0 - f) * omega) / sinomega;
                            beta = sin(f * omega) / sinomega;
                        } else {
                            // Do linear interpolation...
                            alpha = 1.0 - f;
                            beta = f;
                        }
                    } else { // Do linear interpolation...
                        alpha = 1.0 -f;
                        beta = f;
                    }
                    const vec3f vec(alpha * v1->x() + beta * v2->x(),
                                    alpha * v1->y() + beta * v2->y(),
                                    alpha * v1->z() + beta * v2->z());
                    this->value.value[j] = vec;

                    ++v1;
                    ++v2;
                }

                break;
            }
        }
    }

    // Send the new value
    this->emitEvent("value_changed", this->value, timestamp);
}

/**
 * @brief set_key eventIn handler.
 *
 * @param mffloat   an MFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mffloat is not an MFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void NormalInterpolator::processSet_key(const FieldValue & mffloat,
                                        const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->key = dynamic_cast<const MFFloat &>(mffloat);
    this->emitEvent("key_changed", this->key, timestamp);
}

/**
 * @brief set_keyValue eventIn handler.
 *
 * @param mfvec3f   an MFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfvec3f is not an MFVec3f.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void NormalInterpolator::processSet_keyValue(const FieldValue & mfvec3f,
                                             const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->keyValue = dynamic_cast<const MFVec3f &>(mfvec3f);
    this->emitEvent("keyValue_changed", this->keyValue, timestamp);
}


/**
 * @class OrientationInterpolatorClass
 *
 * @brief Class object for OrientationInterpolator nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
OrientationInterpolatorClass::
        OrientationInterpolatorClass(Browser & browser): NodeClass(browser) {}

/**
 * @brief Destructor.
 */
OrientationInterpolatorClass::~OrientationInterpolatorClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating
 *      OrientationInterpolator nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by OrientationInterpolatorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr OrientationInterpolatorClass::
        createType(const std::string & id, const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::sffloat, "set_fraction"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mffloat, "key"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfrotation, "keyValue"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfrotation, "value_changed")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<OrientationInterpolator>(*this, id));
    Vrml97NodeTypeImpl<OrientationInterpolator> & orientationInterpolatorNodeType =
            static_cast<Vrml97NodeTypeImpl<OrientationInterpolator> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<OrientationInterpolator>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            orientationInterpolatorNodeType
                    .addEventIn(supportedInterfaces[0].fieldType,
                                supportedInterfaces[0].id,
                                &OrientationInterpolator::processSet_fraction);
        } else if (*itr == supportedInterfaces[1]) {
            orientationInterpolatorNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &OrientationInterpolator::processSet_key,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<OrientationInterpolator, MFFloat>
                                    (&OrientationInterpolator::key)));
        } else if (*itr == supportedInterfaces[2]) {
            orientationInterpolatorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &OrientationInterpolator::processSet_keyValue,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<OrientationInterpolator, MFRotation>
                                    (&OrientationInterpolator::keyValue)));
        } else if (*itr == supportedInterfaces[3]) {
            orientationInterpolatorNodeType.addEventOut(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<OrientationInterpolator, SFRotation>
                                    (&OrientationInterpolator::value)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class OrientationInterpolator
 *
 * @brief OrientationInterpolator node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
OrientationInterpolator::OrientationInterpolator(const NodeType & nodeType,
                                                 const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractChild(nodeType, scope) {}

/**
 * @brief Destructor.
 */
OrientationInterpolator::~OrientationInterpolator() throw () {}

/**
 * @brief set_fraction eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void OrientationInterpolator::processSet_fraction(const FieldValue & sffloat,
                                                  const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    float f = dynamic_cast<const SFFloat &>(sffloat).value;

    int n = this->key.value.size() - 1;
    if (f < this->key.value[0]) {
        this->value.value = this->keyValue.value[0];
    } else if (f > this->key.value[n]) {
        this->value.value = this->keyValue.value[n];
    } else {
        for (size_t i = 0; i < n; ++i) {
            if (this->key.value[i] <= f && f <= this->key.value[i + 1]) {
                const rotation & v1 = this->keyValue.value[i];
                const rotation & v2 = this->keyValue.value[i + 1];

                // Interpolation factor
                f = (f - this->key.value[i])
                    / (this->key.value[i + 1] - this->key.value[i]);

                float x, y, z, r1, r2;
                r1 = v1[3];

                // Make sure the vectors are not pointing opposite ways
                if (v1[0]*v2[0] + v1[1]*v2[1] + v1[2]*v2[2] < 0.0) {
                    x = v1[0] + f * (-v2[0] - v1[0]);
                    y = v1[1] + f * (-v2[1] - v1[1]);
                    z = v1[2] + f * (-v2[2] - v1[2]);
                    r2 = -v2[3];
                } else {
                    x = v1[0] + f * (v2[0] - v1[0]);
                    y = v1[1] + f * (v2[1] - v1[1]);
                    z = v1[2] + f * (v2[2] - v1[2]);
                    r2 = v2[3];
                }

                // Interpolate angles via the shortest direction
                if (fabs(r2 - r1) > pi) {
                    if (r2 > r1) {
                        r1 += 2.0 * pi;
                    } else {
                        r2 += 2.0 * pi;
                    }
                }
                float angle = r1 + f * (r2 - r1);
                if (angle >= 2.0 * pi) {
                    angle -= 2.0 * pi;
                } else if (angle < 0.0) {
                    angle += 2.0 * pi;
                }
                vec3f Vec(x,y,z);
                Vec = Vec.normalize();
                this->value.value.axis(Vec);
                this->value.value.angle(angle);
                break;
            }
        }
    }

    // Send the new value
    this->emitEvent("value_changed", this->value, timestamp);
}

/**
 * @brief set_key eventIn handler.
 *
 * @param mffloat   an MFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mffloat is not an MFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void OrientationInterpolator::processSet_key(const FieldValue & mffloat,
                                             const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->key = dynamic_cast<const MFFloat &>(mffloat);
    this->emitEvent("key_changed", this->key, timestamp);
}

/**
 * @brief set_keyValue eventIn handler.
 *
 * @param mfrotation    an MFRotation value.
 * @param timestamp     the current time.
 *
 * @exception std::bad_cast     if @p mfrotation is not an MFRotation.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void OrientationInterpolator::processSet_keyValue(const FieldValue & mfrotation,
                                                  const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->keyValue = dynamic_cast<const MFRotation &>(mfrotation);
    this->emitEvent("keyValue_changed", this->keyValue, timestamp);
}


/**
 * @class PixelTextureClass
 *
 * @brief Class object for PixelTexture nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
PixelTextureClass::PixelTextureClass(Browser & browser):
        NodeClass(browser) {}

/**
 * @brief Destructor.
 */
PixelTextureClass::~PixelTextureClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating PixelTexture nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by PixelTextureClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        PixelTextureClass::createType(const std::string & id,
                                      const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfimage, "image"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "repeatS"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "repeatT")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<PixelTexture>(*this, id));
    Vrml97NodeTypeImpl<PixelTexture> & pixelTextureNodeType =
            static_cast<Vrml97NodeTypeImpl<PixelTexture> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<PixelTexture>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            pixelTextureNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &PixelTexture::processSet_image,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PixelTexture, SFImage>
                                    (&PixelTexture::image)));
        } else if (*itr == supportedInterfaces[1]) {
            pixelTextureNodeType.addField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PixelTexture, SFBool>
                                    (&PixelTexture::repeatS)));
        } else if (*itr == supportedInterfaces[2]) {
            pixelTextureNodeType.addField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PixelTexture, SFBool>
                                    (&PixelTexture::repeatT)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class PixelTexture
 *
 * @brief Represents PixelTexture node instances.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node.
 * @param scope     the Scope to which the node belongs.
 */
PixelTexture::PixelTexture(const NodeType & nodeType,
                           const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractTexture(nodeType, scope),
        texObject(0) {}

/**
 * @brief Destructor.
 */
PixelTexture::~PixelTexture() throw ()
{
    // viewer.removeTextureObject(this->texObject); ...
}

/**
 * @brief Render the node.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void PixelTexture::render(Viewer & viewer, const VrmlRenderContext context)
{
    if (isModified()) {
        if (this->texObject) {
            viewer.removeTextureObject(this->texObject);
            this->texObject = 0;
        }
    }

    if (this->image.getPixels()) {
        if (this->texObject) {
            viewer.insertTextureReference(this->texObject,
                                          this->image.getComponents());
        } else {
            // Ensure the image dimensions are powers of two
            const int sizes[] = { 2, 4, 8, 16, 32, 64, 128, 256 };
            const int nSizes = sizeof(sizes) / sizeof(int);
            int w = this->image.getWidth();
            int h = this->image.getHeight();
            int i, j;
            for (i = 0; i < nSizes; ++i) { if (w < sizes[i]) { break; } }
            for (j = 0; j < nSizes; ++j) { if (h < sizes[j]) { break; } }

            if (i > 0 && j > 0) {
                // Always scale images down in size and reuse the same pixel memory.

                // What if we had multiple renderers serving the same scene, and
                // the renderers had different requirements for rescaling the
                // image? I think it would be better to keep the rescaled image
                // in a cache in the renderer.
                //   -- Braden McDaniel <braden@endoframe.com>, 9 Dec, 2000
                if (w != sizes[i - 1] || h != sizes[j - 1]) {
                    const size_t numBytes =
                            this->image.getWidth() * this->image.getHeight()
                                * this->image.getComponents();
                    unsigned char * pixels = new unsigned char[numBytes];
                    std::copy(this->image.getPixels(),
                              this->image.getPixels() + numBytes,
                              pixels);

                    viewer.scaleTexture(w, h, sizes[i - 1], sizes[j - 1],
                                        this->image.getComponents(), pixels);
                    this->image.set(sizes[i - 1], sizes[j - 1],
                                    this->image.getComponents(), pixels);
                    delete [] pixels;
                }

                this->texObject =
                        viewer.insertTexture(this->image.getWidth(),
                                             this->image.getHeight(),
                                             this->image.getComponents(),
                                             this->repeatS.value,
                                             this->repeatT.value,
                                             this->image.getPixels(),
                                             true);
            }
        }
    }
    this->clearModified();
}

size_t PixelTexture::nComponents() const throw () {
    return this->image.getComponents();
}

size_t PixelTexture::width() const throw () { return this->image.getWidth(); }

size_t PixelTexture::height() const throw () { return this->image.getHeight(); }

size_t PixelTexture::nFrames() const throw () { return 0; }

const unsigned char * PixelTexture::pixels() const throw () {
    return this->image.getPixels();
}

/**
 * @brief set_image eventIn handler.
 *
 * @param sfimage   an SFImage value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfimage is not an SFImage.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void PixelTexture::processSet_image(const FieldValue & sfimage,
                                    const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->image = dynamic_cast<const SFImage &>(sfimage);
    this->setModified();
    this->emitEvent("image_changed", this->image, timestamp);
}


/**
 * @class PlaneSensorClass
 *
 * @brief Class object for PlaneSensor nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this node class object.
 */
PlaneSensorClass::PlaneSensorClass(Browser & browser):
        NodeClass(browser) {}

/**
 * @brief Destructor.
 */
PlaneSensorClass::~PlaneSensorClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating PlaneSensor nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by PlaneSensorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        PlaneSensorClass::createType(const std::string & id,
                                     const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "autoOffset"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "enabled"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec2f, "maxPosition"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec2f, "minPosition"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "offset"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isActive"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfvec3f, "trackPoint_changed"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfvec3f, "translation_changed")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<PlaneSensor>(*this, id));
    Vrml97NodeTypeImpl<PlaneSensor> & planeSensorNodeType =
            static_cast<Vrml97NodeTypeImpl<PlaneSensor> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<PlaneSensor>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            planeSensorNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &PlaneSensor::processSet_autoOffset,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PlaneSensor, SFBool>
                                    (&PlaneSensor::autoOffset)));
        } else if (*itr == supportedInterfaces[1]) {
            planeSensorNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &PlaneSensor::processSet_enabled,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PlaneSensor, SFBool>
                                    (&PlaneSensor::enabled)));
        } else if (*itr == supportedInterfaces[2]) {
            planeSensorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &PlaneSensor::processSet_maxPosition,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PlaneSensor, SFVec2f>
                                    (&PlaneSensor::maxPosition)));
        } else if (*itr == supportedInterfaces[3]) {
            planeSensorNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &PlaneSensor::processSet_minPosition,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PlaneSensor, SFVec2f>
                                    (&PlaneSensor::minPosition)));
        } else if (*itr == supportedInterfaces[4]) {
            planeSensorNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &PlaneSensor::processSet_offset,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PlaneSensor, SFVec3f>
                                    (&PlaneSensor::offset)));
        } else if (*itr == supportedInterfaces[5]) {
            planeSensorNodeType.addEventOut(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PlaneSensor, SFBool>
                                    (&PlaneSensor::active)));
        } else if (*itr == supportedInterfaces[6]) {
            planeSensorNodeType.addEventOut(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PlaneSensor, SFVec3f>
                                    (&PlaneSensor::trackPoint)));
        } else if (*itr == supportedInterfaces[7]) {
            planeSensorNodeType.addEventOut(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PlaneSensor, SFVec3f>
                                    (&PlaneSensor::translation)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class PlaneSensor
 *
 * The PlaneSensor node maps pointing device motion into
 * two-dimensional translation in a plane parallel to the Z=0 plane of
 * the local coordinate system. The PlaneSensor node uses the
 * descendent geometry of its parent node to determine whether it is
 * liable to generate events
 *
 * @todo need copy constructor for d_parentTransform ...
 */

/**
 * @var PlaneSensor::PlaneSensorClass
 *
 * @brief Class object for PlaneSensor instances.
 */

/**
 * @var SFBool PlaneSensor::autoOffset
 *
 * @brief autoOffset exposedField.
 */

/**
 * @var SFBool PlaneSensor::enabled
 *
 * @brief enabled exposedField.
 */

/**
 * @var SFVec2f PlaneSensor::maxPosition
 *
 * @brief maxPosition exposedField.
 */

/**
 * @var SFVec2f PlaneSensor::minPosition
 *
 * @brief minPosition exposedField.
 */

/**
 * @var SFVec3f PlaneSensor::offset
 *
 * @brief offset exposedField.
 */

/**
 * @var SFBool PlaneSensor::active
 *
 * @brief isActive eventOut.
 */

/**
 * @var SFVec3f PlaneSensor::trackPoint
 *
 * @brief trackPoint_changed eventOut.
 */

/**
 * @var SFVec3f PlaneSensor::translation
 *
 * @brief translation_changed eventOut.
 */

/**
 * @var SFVec3f PlaneSensor::activationPoint
 *
 * @brief The point at which the PlaneSensor was activated.
 */

/**
 * @var Node * PlaneSensor::parentTransform
 *
 * @brief The parent Transform.
 */

/**
 * @var mat4f PlaneSensor::activationMatrix
 */

/**
 * @var mat4f PlaneSensor::modelview
 *
 * @brief The modelview matrix.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node.
 * @param scope     the Scope to which the node belongs.
 */
PlaneSensor::PlaneSensor(const NodeType & nodeType,
                         const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope),
    autoOffset(true),
    enabled(true),
    maxPosition(vec2f(-1.0, -1.0)),
    minPosition(vec2f(0.0, 0.0)),
    offset(vec3f(0.0, 0.0, 0.0)),
    active(false)
{
    this->setModified();
}

/**
 * @brief Destructor.
 */
PlaneSensor::~PlaneSensor() throw ()
{}

/**
 * @brief Cast to a PlaneSensor.
 *
 * @return a pointer to the PlaneSensor.
 */
PlaneSensor * PlaneSensor::toPlaneSensor() const
{
    return (PlaneSensor*) this;
}

/**
 * @brief Render the node.
 *
 * Render a frame if there is one available.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void PlaneSensor::render(Viewer & viewer, const VrmlRenderContext context)
{
    //
    // Store the ModelView matrix which is calculated at the time of rendering
    // in render-context. This matrix will be in use at the time of activation.
    //
    this->modelview = context.getMatrix();
}

/**
 * @todo This is not correct. The local coords are computed for one instance,
 * need to convert p to local coords for each instance (DEF/USE) of the
 * sensor...
 */
void PlaneSensor::activate(double timeStamp, bool isActive, double * p)
{
    // Become active
    if (isActive && !this->active.value) {
        this->active.value = isActive;

        vec3f V(p[0], p[1], p[2]);
        this->activationMatrix = this->modelview.inverse();
        V = V * this->activationMatrix;
        this->activationPoint.value = V;
        this->emitEvent("isActive", this->active, timeStamp);
    }

    // Become inactive
    else if (!isActive && this->active.value) {
        this->active.value = isActive;
        this->emitEvent("isActive", this->active, timeStamp);

        // auto offset
        if (this->autoOffset.value) {
            this->offset = this->translation;
            this->emitEvent("offset_changed", this->offset, timeStamp);
        }
    }

    // Tracking
    else if (isActive) {
        vec3f V(p[0], p[1], p[2]);
        V = V * this->activationMatrix;
        this->trackPoint.value = V;
        this->emitEvent("trackPoint_changed", this->trackPoint, timeStamp);

        vec3f t(V[0] - this->activationPoint.value.x() + this->offset.value.x(),
                V[1] - this->activationPoint.value.y() + this->offset.value.y(),
                0.0);

        if (this->minPosition.value.x() == this->maxPosition.value.x() ) {
            t.x(this->minPosition.value.x());
        } else if (this->minPosition.value.x() < this->maxPosition.value.x()) {
            if (t.x() < this->minPosition.value.x()) {
                t.x(this->minPosition.value.x());
            } else if (t.x() > this->maxPosition.value.x()) {
                t.x(this->maxPosition.value.x());
            }
        }

        if (this->minPosition.value.y() == this->maxPosition.value.y()) {
            t.y(this->minPosition.value.y());
        } else if (this->minPosition.value.y() < this->maxPosition.value.y()) {
            if (t.y() < this->minPosition.value.y()) {
                t.y(this->minPosition.value.y());
            } else if (t.y() > this->maxPosition.value.y()) {
                t.y(this->maxPosition.value.y());
            }
        }

        this->translation.value = t;
        this->emitEvent("translation_changed", this->translation, timeStamp);
    }
}

/**
 * @brief set_autoOffset eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void PlaneSensor::processSet_autoOffset(const FieldValue & sfbool,
                                        const double timestamp)
        throw (std::bad_cast) {
    this->autoOffset = dynamic_cast<const SFBool &>(sfbool);
    this->setModified();
    this->emitEvent("autoOffset_changed", this->autoOffset, timestamp);
}

/**
 * @brief set_enabled eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void PlaneSensor::processSet_enabled(const FieldValue & sfbool,
                                     const double timestamp)
        throw (std::bad_cast) {
    this->enabled = dynamic_cast<const SFBool &>(sfbool);
    this->setModified();
    this->emitEvent("enabled_changed", this->enabled, timestamp);
}

/**
 * @brief set_maxPosition eventIn handler.
 *
 * @param sfvec2f   an SFVec2f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec2f is not an SFVec2f.
 */
void PlaneSensor::processSet_maxPosition(const FieldValue & sfvec2f,
                                         const double timestamp)
        throw (std::bad_cast) {
    this->maxPosition = dynamic_cast<const SFVec2f &>(sfvec2f);
    this->setModified();
    this->emitEvent("maxPosition_changed", this->maxPosition, timestamp);
}

/**
 * @brief set_minPosition eventIn handler.
 *
 * @param sfvec2f   an SFVec2f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec2f is not an SFVec2f.
 */
void PlaneSensor::processSet_minPosition(const FieldValue & sfvec2f,
                                         const double timestamp)
        throw (std::bad_cast) {
    this->minPosition = dynamic_cast<const SFVec2f &>(sfvec2f);
    this->setModified();
    this->emitEvent("minPosition_changed", this->minPosition, timestamp);
}

/**
 * @brief set_offset eventIn handler.
 *
 * @param sfvec3f   an SFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void PlaneSensor::processSet_offset(const FieldValue & sfvec3f,
                                    const double timestamp)
        throw (std::bad_cast) {
    this->offset = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("offset_changed", this->offset, timestamp);
}


/**
 * @class PointLightClass
 *
 * @brief Class object for PointLight nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this class object.
 */
PointLightClass::PointLightClass(Browser & browser):
        NodeClass(browser) {}

/**
 * @brief Destructor.
 */
PointLightClass::~PointLightClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating PointLight nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by PointLightClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
PointLightClass::createType(const std::string & id,
                            const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "ambientIntensity"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "attenuation"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfcolor, "color"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "intensity"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "location"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "on"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "radius")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<PointLight>(*this, id));
    Vrml97NodeTypeImpl<PointLight> & pointLightNodeType =
            static_cast<Vrml97NodeTypeImpl<PointLight> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<PointLight>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            pointLightNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &PointLight::processSet_ambientIntensity,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PointLight, SFFloat>
                                    (&PointLight::ambientIntensity)));
        } else if (*itr == supportedInterfaces[1]) {
            pointLightNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &PointLight::processSet_attenuation,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PointLight, SFVec3f>
                                    (&PointLight::attenuation)));
        } else if (*itr == supportedInterfaces[2]) {
            pointLightNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &PointLight::processSet_color,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PointLight, SFColor>
                                    (&PointLight::color)));
        } else if (*itr == supportedInterfaces[3]) {
            pointLightNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &PointLight::processSet_intensity,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PointLight, SFFloat>
                                    (&PointLight::intensity)));
        } else if (*itr == supportedInterfaces[4]) {
            pointLightNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &PointLight::processSet_location,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PointLight, SFVec3f>
                                    (&PointLight::location)));
        } else if (*itr == supportedInterfaces[5]) {
            pointLightNodeType.addExposedField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                &PointLight::processSet_on,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PointLight, SFBool>
                                    (&PointLight::on)));
        } else if (*itr == supportedInterfaces[6]) {
            pointLightNodeType.addExposedField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                &PointLight::processSet_radius,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PointLight, SFFloat>
                                    (&PointLight::radius)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class PointLight
 *
 * @brief PointLight node instances.
 */

/**
 * @var PointLight::PointLightClass
 *
 * @brief Class object for PointLight instances.
 */

/**
 * @var SFVec3f PointLight::attenuation
 *
 * @brief attenuation exposedField.
 */

/**
 * @var SFVec3f PointLight::location
 *
 * @brief location exposedField.
 */

/**
 * @var SFFloat PointLight::radius
 *
 * @brief radius exposedField.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
PointLight::PointLight(const NodeType & nodeType,
                       const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractLight(nodeType, scope),
    attenuation(vec3f(1.0, 0.0, 0.0)),
    location(vec3f(0.0, 0.0, 0.0)),
    radius(100)
{}

/**
 * @brief Destructor.
 */
PointLight::~PointLight() throw ()
{}

/**
 * @brief Cast to a PointLight.
 *
 * @return a pointer to the PointLight.
 */
PointLight* PointLight::toPointLight() const
{
    return (PointLight*) this;
}

/**
 * @brief Render the scoped light.
 *
 * This should be called before rendering any geometry nodes in the scene.
 * Since this is called from Scene::render() before traversing the
 * scene graph, the proper transformation matrix hasn't been set up.
 * Somehow it needs to figure out the accumulated xforms of its
 * parents and apply them before rendering. This is not easy with
 * DEF/USEd nodes...
 *
 * @param viewer    a Viewer.
 */
void PointLight::renderScoped(Viewer & viewer)
{
    if (this->on.value && this->radius.value > 0.0) {
        viewer.insertPointLight(this->ambientIntensity.value,
                                this->attenuation.value,
                                this->color.value,
                                this->intensity.value,
                                this->location.value,
                                this->radius.value);
    }
    this->clearModified();
}

/**
 * @brief Initialize.
 *
 * @param timestamp the current time.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void PointLight::do_initialize(const double timestamp) throw (std::bad_alloc)
{
    assert(this->getScene());
    this->getScene()->browser.addScopedLight(*this);
}

/**
 * @brief Shut down.
 *
 * @param timestamp the current time.
 */
void PointLight::do_shutdown(const double timestamp) throw ()
{
    assert(this->getScene());
    this->getScene()->browser.removeScopedLight(*this);
}

/**
 * @brief set_attenuation eventIn handler.
 *
 * @param sfvec3f   an SFVec3f.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void PointLight::processSet_attenuation(const FieldValue & sfvec3f,
                                        const double timestamp)
    throw (std::bad_cast)
{
    this->attenuation = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("attenuation_changed", this->attenuation, timestamp);
}

/**
 * @brief set_location eventIn handler.
 *
 * @param sfvec3f   an SFVec3f.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void PointLight::processSet_location(const FieldValue & sfvec3f,
                                     const double timestamp)
    throw (std::bad_cast)
{
    this->location = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("location_changed", this->location, timestamp);
}

/**
 * @brief set_radius eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void PointLight::processSet_radius(const FieldValue & sffloat,
                                   const double timestamp)
    throw (std::bad_cast)
{
    this->radius = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("radius_changed", this->radius, timestamp);
}


/**
 * @class PointSetClass
 *
 * @brief Class object for @link PointSet PointSets@endlink.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
PointSetClass::PointSetClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
PointSetClass::~PointSetClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating PointSet nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by PointSetClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr PointSetClass::createType(const std::string & id,
                                            const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "color"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "coord")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<PointSet>(*this, id));
    Vrml97NodeTypeImpl<PointSet> & pointSetNodeType =
            static_cast<Vrml97NodeTypeImpl<PointSet> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<PointSet>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            pointSetNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &PointSet::processSet_color,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PointSet, SFNode>
                                    (&PointSet::color)));
        } else if (*itr == supportedInterfaces[1]) {
            pointSetNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &PointSet::processSet_coord,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PointSet, SFNode>
                                    (&PointSet::coord)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class PointSet
 *
 * @brief Represents PointSet node instances.
 */

/**
 * @var PointSet::PointSetClass
 *
 * @brief Class object for PointSet instances.
 */

/**
 * @var SFNode PointSet::color
 *
 * @brief color exposedField.
 */

/**
 * @var SFNode PointSet::coord
 *
 * @brief coord exposedField.
 */

/**
 * @var BSphere PointSet::bsphere
 *
 * @brief Bounding volume.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node.
 * @param scope     the Scope to which the node belongs.
 */
PointSet::PointSet(const NodeType & nodeType,
                   const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractGeometry(nodeType, scope)
{
    this->setBVolumeDirty(true);
}

/**
 * @brief Destructor.
 */
PointSet::~PointSet() throw ()
{}

/**
 * @brief Determine whether the node has been modified.
 *
 * @return @c true if the node or one of its children has been modified,
 *      @c false otherwise.
 */
bool PointSet::isModified() const
{
    return (d_modified
            || (this->color.value && this->color.value->isModified())
            || (this->coord.value && this->coord.value->isModified()));
}

/**
 * @brief Propagate the bvolume dirty flag from children to parents.
 *
 * @param path  stack of ancestor nodes.
 * @param flags 1 indicates normal modified flag, 2 indicates the
 *              bvolume dirty flag, 3 indicates both.
 */
void PointSet::updateModified(NodePath & path, int flags)
{
    if (this->isModified()) { markPathModified(path, true); }
    path.push_front(this);
    if (this->color.value) { this->color.value->updateModified(path); }
    if (this->coord.value) { this->coord.value->updateModified(path); }
    path.pop_front();
}

/**
 * @brief Insert this geometry into @p viewer's display list.
 *
 * @param viewer    a Viewer.
 * @param context   the rendering context.
 */
Viewer::Object PointSet::insertGeometry(Viewer & viewer,
                                        const VrmlRenderContext context)
{
    using std::vector;
    
    if (context.getDrawBSpheres()) {
        const BSphere * bs = (const BSphere*)this->getBVolume();
        viewer.drawBSphere(*bs, static_cast<BVolume::Intersection>(4));
    }

    CoordinateNode * const coordinateNode = this->coord.value
                                          ? this->coord.value->toCoordinate()
                                          : 0;
    const vector<vec3f> & coord = coordinateNode
                                ? coordinateNode->getPoint()
                                : vector<vec3f>();

    ColorNode * const colorNode = this->color.value
                                ? this->color.value->toColor()
                                : 0;
    const vector<OpenVRML::color> & color = colorNode
                                          ? colorNode->getColor()
                                          : vector<OpenVRML::color>();

    Viewer::Object obj = viewer.insertPointSet(coord, color);

    if (this->color.value) { this->color.value->clearModified(); }
    if (this->coord.value) { this->coord.value->clearModified(); }

    return obj;
}

/**
 * @brief Recalculate the bounding volume.
 */
void PointSet::recalcBSphere()
{
    this->bsphere.reset();
    CoordinateNode * const coordinateNode = this->coord.value
                                          ? this->coord.value->toCoordinate()
                                          : 0;
    if (coordinateNode) {
        const std::vector<vec3f> & coord = coordinateNode->getPoint();
        for(std::vector<vec3f>::const_iterator vec(coord.begin());
                vec != coord.end(); ++vec) {
            this->bsphere.extend(*vec);
        }
    }
    this->setBVolumeDirty(false);
}

/**
 * @brief Get the bounding volume.
 *
 * @return the bounding volume associated with the node.
 */
const BVolume* PointSet::getBVolume() const
{
    if (this->isBVolumeDirty()) {
        ((PointSet*)this)->recalcBSphere();
    }
    return &this->bsphere;
}

/**
 * @brief set_color eventIn handler.
 *
 * @param sfnode    an SFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void PointSet::processSet_color(const FieldValue & sfnode,
                                const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->color = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("color_changed", this->color, timestamp);
}

/**
 * @brief set_coord eventIn handler.
 *
 * @param sfnode    an SFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void PointSet::processSet_coord(const FieldValue & sfnode,
                                const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->coord = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("coord_changed", this->coord, timestamp);
}


/**
 * @class PositionInterpolatorClass
 *
 * @brief Class object for @link PositionInterpolator PositionInterpolators@endlink.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
PositionInterpolatorClass::PositionInterpolatorClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
PositionInterpolatorClass::~PositionInterpolatorClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating
 *      CoordinateInterpolator nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                                  supported by CoordinateInterpolatorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
PositionInterpolatorClass::createType(const std::string & id,
                                      const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::sffloat, "set_fraction"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mffloat, "key"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfvec3f, "keyValue"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfvec3f, "value_changed")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<PositionInterpolator>(*this, id));
    Vrml97NodeTypeImpl<PositionInterpolator> & positionInterpolatorNodeType =
            static_cast<Vrml97NodeTypeImpl<PositionInterpolator> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<PositionInterpolator>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            positionInterpolatorNodeType
                    .addEventIn(supportedInterfaces[0].fieldType,
                                supportedInterfaces[0].id,
                                &PositionInterpolator::processSet_fraction);
        } else if (*itr == supportedInterfaces[1]) {
            positionInterpolatorNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &PositionInterpolator::processSet_key,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PositionInterpolator, MFFloat>
                                    (&PositionInterpolator::key)));
        } else if (*itr == supportedInterfaces[2]) {
            positionInterpolatorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &PositionInterpolator::processSet_keyValue,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PositionInterpolator, MFVec3f>
                                    (&PositionInterpolator::keyValue)));
        } else if (*itr == supportedInterfaces[3]) {
            positionInterpolatorNodeType.addEventOut(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<PositionInterpolator, SFVec3f>
                                    (&PositionInterpolator::value)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class PositionInterpolator
 *
 * @brief PositionInterpolator node instances.
 */

/**
 * @var PositionInterpolator::PositionInterpolatorClass
 *
 * @brief Class object for PositionInterpolator instances.
 */

/**
 * @var MFFloat PositionInterpolator::key
 *
 * @brief key exposedField.
 */

/**
 * @var MFVec3f PositionInterpolator::keyValue
 *
 * @brief keyValue exposedField.
 */

/**
 * @var SFVec3f PositionInterpolator::value
 *
 * @brief value_changed eventOut.
 */


/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
PositionInterpolator::PositionInterpolator(const NodeType & nodeType,
                                           const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope)
{}

/**
 * @brief Destructor.
 */
PositionInterpolator::~PositionInterpolator() throw ()
{}

/**
 * @brief set_fraction eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void PositionInterpolator::processSet_fraction(const FieldValue & sffloat,
                                               const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    float f = dynamic_cast<const SFFloat &>(sffloat).value;

    int n = this->key.value.size() - 1;
    if (f < this->key.value[0]) {
        this->value.value = this->keyValue.value[0];
    } else if (f > this->key.value[n]) {
        this->value.value = this->keyValue.value[n];
    } else {
        // should cache the last index used...
        for (int i = 0; i < n; ++i) {
            if (this->key.value[i] <= f && f <= this->key.value[i + 1]) {
                const vec3f & v1 = this->keyValue.value[i];
                const vec3f & v2 = this->keyValue.value[i + 1];

                f = (f - this->key.value[i])
                    / (this->key.value[i + 1] - this->key.value[i]);
                const vec3f valueVec(v1.x() + f * (v2.x() - v1.x()),
                                     v1.y() + f * (v2.y() - v1.y()),
                                     v1.z() + f * (v2.z() - v1.z()));
                this->value.value = valueVec;
                break;
            }
        }
    }

    // Send the new value
    this->emitEvent("value_changed", this->value, timestamp);
}

/**
 * @brief set_key eventIn handler.
 *
 * @param mffloat   an MFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mffloat is not an MFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void PositionInterpolator::processSet_key(const FieldValue & mffloat,
                                          const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->key = dynamic_cast<const MFFloat &>(mffloat);
    this->emitEvent("key_changed", this->key, timestamp);
}

/**
 * @brief set_keyValue field mutator.
 *
 * @param mfvec3f   an MFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfvec3f is not an MFVec3f.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void PositionInterpolator::processSet_keyValue(const FieldValue & mfvec3f,
                                               const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->keyValue = dynamic_cast<const MFVec3f &>(mfvec3f);
    this->emitEvent("keyValue_changed", this->keyValue, timestamp);
}


/**
 * @class ProximitySensorClass
 *
 * @brief Class object for @link ProximitySensor ProximitySensors@endlink.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
ProximitySensorClass::ProximitySensorClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
ProximitySensorClass::~ProximitySensorClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating ProximitySensor
 *      nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by ProximitySensorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
ProximitySensorClass::createType(const std::string & id,
                                 const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "center"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "size"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "enabled"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isActive"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfvec3f, "position_changed"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfrotation, "orientation_changed"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sftime, "enterTime"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sftime, "exitTime")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<ProximitySensor>(*this, id));
    Vrml97NodeTypeImpl<ProximitySensor> & proximitySensorNodeType =
            static_cast<Vrml97NodeTypeImpl<ProximitySensor> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<ProximitySensor>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            proximitySensorNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &ProximitySensor::processSet_center,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ProximitySensor, SFVec3f>
                                    (&ProximitySensor::center)));
        } else if (*itr == supportedInterfaces[1]) {
            proximitySensorNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &ProximitySensor::processSet_size,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ProximitySensor, SFVec3f>
                                    (&ProximitySensor::size)));
        } else if (*itr == supportedInterfaces[2]) {
            proximitySensorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &ProximitySensor::processSet_enabled,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ProximitySensor, SFBool>
                                    (&ProximitySensor::enabled)));
        } else if (*itr == supportedInterfaces[3]) {
            proximitySensorNodeType.addEventOut(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ProximitySensor, SFBool>
                                    (&ProximitySensor::active)));
        } else if (*itr == supportedInterfaces[4]) {
            proximitySensorNodeType.addEventOut(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ProximitySensor, SFVec3f>
                                    (&ProximitySensor::position)));
        } else if (*itr == supportedInterfaces[5]) {
            proximitySensorNodeType.addEventOut(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ProximitySensor, SFRotation>
                                    (&ProximitySensor::orientation)));
        } else if (*itr == supportedInterfaces[6]) {
            proximitySensorNodeType.addEventOut(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ProximitySensor, SFTime>
                                    (&ProximitySensor::enterTime)));
        } else if (*itr == supportedInterfaces[7]) {
            proximitySensorNodeType.addEventOut(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ProximitySensor, SFTime>
                                    (&ProximitySensor::exitTime)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class ProximitySensor
 *
 * @brief Represents ProximitySensor node instances.
 */

/**
 * @var ProximitySensor::ProximitySensorClass
 *
 * @brief Class object for ProximitySensor instances.
 */

/**
 * @var SFVec3f ProximitySensor::center
 *
 * @brief center exposedField.
 */

/**
 * @var SFBool ProximitySensor::enabled
 *
 * @brief enabled exposedField.
 */

/**
 * @var SFVec3f ProximitySensor::size
 *
 * @brief size exposedField.
 */

/**
 * @var SFBool ProximitySensor::active
 *
 * @brief isActive eventOut.
 */

/**
 * @var SFVec3f ProximitySensor::position
 *
 * @brief position_changed eventOut.
 */

/**
 * @var SFVec3f ProximitySensor::orientation
 *
 * @brief orientation_changed eventOut.
 */

/**
 * @var SFTime ProximitySensor::enterTime
 *
 * @brief enterTime eventOut.
 */

/**
 * @var SFTime ProximitySensor::exitTime
 *
 * @brief exitTime eventOut.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node.
 * @param scope     the Scope to which the node belongs.
 */
ProximitySensor::ProximitySensor(const NodeType & nodeType,
                                 const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope),
    center(vec3f(0.0, 0.0, 0.0)),
    enabled(true),
    size(vec3f(0.0, 0.0, 0.0)),
    active(false),
    position(vec3f(0.0, 0.0, 0.0)),
    enterTime(0.0),
    exitTime(0.0)
{
    this->setModified();
}

/**
 * @brief Destructor.
 */
ProximitySensor::~ProximitySensor() throw ()
{}

/**
 * @brief Render the node: generate proximity events.
 *
 * If necessary, events prior to the current time are generated due to
 * interpolation of enterTimes and exitTimes. The timestamp should never be
 * increased.
 *
 * This is in a render() method since the it needs the viewer position
 * with respect to the local coordinate system.
 * Could do this with Node::inverseTransform(double [4][4]) now...
 *
 * The positions and times are not interpolated to report the exact
 * place and time of entries and exits from the sensor regions as
 * required by the spec, since that would require the last viewer position
 * to be stored in the node, which is problematic in the presence of
 * DEF/USEd nodes...
 * I suppose the scene could keep the last viewer position in the global
 * coordinate system and it could be transformed all the way down the
 * scenegraph, but that sounds painful.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void ProximitySensor::render(Viewer & viewer, const VrmlRenderContext context)
{
    using OpenVRML_::fpequal;

    if (this->enabled.value
            && this->size.value.x() > 0.0
            && this->size.value.y() > 0.0
            && this->size.value.z() > 0.0
            && viewer.getRenderMode() == Viewer::RENDER_MODE_DRAW) {
        SFTime timeNow(Browser::getCurrentTime());
        float x, y, z;

        // Is viewer inside the box?
        mat4f MV = context.getMatrix();
        MV = MV.inverse();
        x = MV[3][0]; y = MV[3][1]; z = MV[3][2];
        bool inside = (fabs(x - this->center.value.x())
                            <= 0.5 * this->size.value.x()
                        && fabs(y - this->center.value.y())
                            <= 0.5 * this->size.value.y()
                        && fabs(z - this->center.value.z())
                            <= 0.5 * this->size.value.z());
        bool wasIn = this->active.value;

        // Check if viewer has entered the box
        if (inside && ! wasIn) {
            this->active.value = true;
            this->emitEvent("isActive", this->active, timeNow.value);

            this->enterTime = timeNow;
            this->emitEvent("enterTime", this->enterTime, timeNow.value);
        }

        // Check if viewer has left the box
        else if (wasIn && !inside) {
            this->active.value = false;
            this->emitEvent("isActive", this->active, timeNow.value);

            this->exitTime = timeNow;
            this->emitEvent("exitTime", this->exitTime, timeNow.value);
        }

        // Check for movement within the box
        if (wasIn || inside) {
            if (position.value != vec3f(x, y, z)) {
                this->position.value = vec3f(x, y, z);
                this->emitEvent("position_changed", this->position,
                                timeNow.value);
            }

            vec3f trans, scale, shear;
            rotation orientation;
            MV.transformation(trans, orientation, scale, shear);
            if (this->orientation.value != orientation) {
                this->orientation.value = orientation;
                this->emitEvent("orientation_changed", this->orientation,
                                timeNow.value);
            }
        }
    } else {
        this->clearModified();
    }
}

/**
 * @brief set_center eventIn handler.
 *
 * @param sfvec3f   an SFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void ProximitySensor::processSet_center(const FieldValue & sfvec3f,
                                        const double timestamp)
    throw (std::bad_cast)
{
    this->center = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("center_changed", this->center, timestamp);
}

/**
 * @brief set_size eventIn handler.
 *
 * @param sfvec3f   an SFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void ProximitySensor::processSet_size(const FieldValue & sfvec3f,
                                      const double timestamp)
    throw (std::bad_cast)
{
    this->size = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("size_changed", this->size, timestamp);
}

/**
 * @brief set_enabled eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void ProximitySensor::processSet_enabled(const FieldValue & sfbool,
                                         double timestamp)
    throw (std::bad_cast)
{
    this->enabled = dynamic_cast<const SFBool &>(sfbool);
    this->setModified();
    this->emitEvent("enabled_changed", this->enabled, timestamp);
}


/**
 * @class ScalarInterpolatorClass
 *
 * @brief Class object for @link ScalarInterpolator ScalarInterpolators@endlink.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
ScalarInterpolatorClass::ScalarInterpolatorClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
ScalarInterpolatorClass::~ScalarInterpolatorClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating
 *      CoordinateInterpolator nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by CoordinateInterpolatorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
ScalarInterpolatorClass::createType(const std::string & id,
                                    const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::sffloat, "set_fraction"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mffloat, "key"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mffloat, "keyValue"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sffloat, "value_changed")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<ScalarInterpolator>(*this, id));
    Vrml97NodeTypeImpl<ScalarInterpolator> & scalarInterpolatorNodeType =
            static_cast<Vrml97NodeTypeImpl<ScalarInterpolator> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<ScalarInterpolator>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            scalarInterpolatorNodeType
                    .addEventIn(supportedInterfaces[0].fieldType,
                                supportedInterfaces[0].id,
                                &ScalarInterpolator::processSet_fraction);
        } else if (*itr == supportedInterfaces[1]) {
            scalarInterpolatorNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &ScalarInterpolator::processSet_key,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ScalarInterpolator, MFFloat>
                                    (&ScalarInterpolator::key)));
        } else if (*itr == supportedInterfaces[2]) {
            scalarInterpolatorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &ScalarInterpolator::processSet_keyValue,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ScalarInterpolator, MFFloat>
                                    (&ScalarInterpolator::keyValue)));
        } else if (*itr == supportedInterfaces[3]) {
            scalarInterpolatorNodeType.addEventOut(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<ScalarInterpolator, SFFloat>
                                    (&ScalarInterpolator::value)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class ScalarInterpolator
 *
 * @brief ScalarInterpolator node instances.
 */

/**
 * @var ScalarInterpolator::ScalarInterpolatorClass
 *
 * @brief Class object for ScalarInterpolator instances.
 */

/**
 * @var MFFloat ScalarInterpolator::key
 *
 * @brief key exposedField.
 */

/**
 * @var MFFloat ScalarInterpolator::keyValue
 *
 * @brief keyValue exposedField.
 */

/**
 * @var SFFloat ScalarInterpolator::value
 *
 * @brief value_changed eventOut.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
ScalarInterpolator::ScalarInterpolator(const NodeType & nodeType,
                                       const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope)
{}

/**
 * @brief Destructor.
 */
ScalarInterpolator::~ScalarInterpolator() throw ()
{}

/**
 * @brief set_fraction eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void ScalarInterpolator::processSet_fraction(const FieldValue & sffloat,
                                             const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    float f = dynamic_cast<const SFFloat &>(sffloat).value;

    int n = this->key.value.size() - 1;
    if (f < this->key.value[0]) {
        this->value.value = this->keyValue.value[0];
    } else if (f > this->key.value[n]) {
        this->value.value = this->keyValue.value[n];
    } else {
        for (int i=0; i<n; ++i) {
            if (this->key.value[i] <= f && f <= this->key.value[i + 1]) {
                float v1 = this->keyValue.value[i];
                float v2 = this->keyValue.value[i + 1];

                f = (f - this->key.value[i]) / (this->key.value[i + 1]
                        - this->key.value[i]);
                this->value.value = v1 + f * (v2 - v1);
                break;
            }
        }
    }

    // Send the new value
    this->emitEvent("value_changed", this->value, timestamp);
}

/**
 * @brief set_key eventIn handler.
 *
 * @param mffloat   an MFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mffloat is not an MFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void ScalarInterpolator::processSet_key(const FieldValue & mffloat,
                                        const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->key = dynamic_cast<const MFFloat &>(mffloat);
    this->emitEvent("key_changed", this->key, timestamp);
}

/**
 * @brief set_keyValue field mutator.
 *
 * @param mffloat   an MFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mffloat is not an MFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void ScalarInterpolator::processSet_keyValue(const FieldValue & mffloat,
                                             const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->keyValue = dynamic_cast<const MFFloat &>(mffloat);
    this->emitEvent("keyValue_changed", this->keyValue, timestamp);
}


/**
 * @class ShapeClass
 *
 * @brief Class object for @link Shape Shape@endlink.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
ShapeClass::ShapeClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
ShapeClass::~ShapeClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Shape nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by ShapeClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr ShapeClass::createType(const std::string & id,
                                         const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "appearance"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "geometry")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Shape>(*this, id));
    Vrml97NodeTypeImpl<Shape> & shapeNodeType =
            static_cast<Vrml97NodeTypeImpl<Shape> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Shape>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            shapeNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &Shape::processSet_appearance,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Shape, SFNode>
                                    (&Shape::appearance)));
        } else if (*itr == supportedInterfaces[1]) {
            shapeNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &Shape::processSet_geometry,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Shape, SFNode>
                                    (&Shape::geometry)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Shape
 *
 * @brief Represents Shape node instances.
 */

/**
 * @var Shape::ShapeClass
 *
 * @brief Class object for Shape instances.
 */

/**
 * @var SFNode Shape::appearance
 *
 * @brief appearance exposedField.
 */

/**
 * @var SFNode Shape::geometry
 *
 * @brief geometry exposedField.
 */

/**
 * @var Viewer::Object Shape::viewerObject
 *
 * @brief A reference to the node's previously used rendering data.
 *
 * If supported by the Viewer implementation, this member holds a reference
 * to the node's rendering data once the node has already been rendered once.
 * The intent is to capitalize on USE references in the VRML scene graph.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node.
 * @param scope     the Scope to which the node belongs.
 */
Shape::Shape(const NodeType & nodeType,
             const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope),
    viewerObject(0)
{}

/**
 * @brief Destructor.
 */
Shape::~Shape() throw ()
{
    // need viewer to free viewerObject ...
}

/**
 * @brief Determine whether the node has been modified.
 *
 * @return @c true if the node or one of its children has been modified,
 *      @c false otherwise.
 */
bool Shape::isModified() const
{
    return (d_modified
            || (this->geometry.value && this->geometry.value->isModified())
            || (this->appearance.value && this->appearance.value->isModified()));
}

/**
 * @brief Propagate the bvolume dirty flag from children to parents.
 *
 * @param path  stack of ancestor nodes.
 * @param flags 1 indicates normal modified flag, 2 indicates the
 *              bvolume dirty flag, 3 indicates both.
 */
void Shape::updateModified(NodePath & path, int flags)
{
    if (this->isModified()) { markPathModified(path, true, flags); }
    path.push_front(this);
    if (this->appearance.value) {
        this->appearance.value->updateModified(path, flags);
    }
    if (this->geometry.value) {
        this->geometry.value->updateModified(path, flags);
    }
    path.pop_front();
}

/**
 * @brief Render the node.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void Shape::render(Viewer & viewer, const VrmlRenderContext context)
{
    if (this->viewerObject && isModified()) {
        viewer.removeObject(this->viewerObject);
        this->viewerObject = 0;
    }

    GeometryNode * g = this->geometry.value
                     ? this->geometry.value->toGeometry()
                     : 0;

    if (this->viewerObject) {
        viewer.insertReference(this->viewerObject);
    } else if (g) {
        this->viewerObject = viewer.beginObject(this->getId().c_str());

        // Don't care what color it is if we are picking
        bool picking = (Viewer::RENDER_MODE_PICK == viewer.getRenderMode());
        if (!picking) {
            int nTexComponents = 0;

            if (!picking && this->appearance.value
                    && this->appearance.value->toAppearance()) {
                AppearanceNode * a = this->appearance.value->toAppearance();
                a->render(viewer, context);

                if (a->getTexture() && a->getTexture()->toTexture()) {
                    nTexComponents =
                            a->getTexture()->toTexture()->nComponents();
                }
            } else {
                viewer.setColor(color(1.0, 1.0, 1.0)); // default object color
                viewer.enableLighting(false);  // turn lighting off
            }

            // hack for opengl material mode
            viewer.setMaterialMode(nTexComponents, g->getColor());
        }

        g->render(viewer, context);

        viewer.endObject();
    } else if (this->appearance.value) {
        this->appearance.value->clearModified();
    }
    this->clearModified();
}

/**
 * @brief Get the bounding volume.
 *
 * @return the bounding volume associated with the node.
 */
const BVolume* Shape::getBVolume() const
{
    //
    // just pass off to the geometry's getbvolume() method
    //
    const BVolume * r = 0;
    const NodePtr & geom = this->geometry.value;
    if (geom) { r = geom->getBVolume(); }
    ((Shape*)this)->setBVolumeDirty(false);
    return r;
}

/**
 * @brief set_appearance eventIn handler.
 *
 * @param sfnode    an SFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Shape::processSet_appearance(const FieldValue & sfnode,
                                  const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->appearance = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("appearance_changed", this->appearance, timestamp);
}

/**
 * @brief set_geometry eventIn handler.
 *
 * @param sfnode    an SFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Shape::processSet_geometry(const FieldValue & sfnode,
                                const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->geometry = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("geometry_changed", this->geometry, timestamp);
}


/**
 * @class SoundClass
 *
 * @brief Class object for @link Sound Sound@endlink.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
SoundClass::SoundClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
SoundClass::~SoundClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Sound nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by SoundClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr SoundClass::createType(const std::string & id,
                                         const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "direction"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "intensity"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "location"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "maxBack"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "maxFront"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "minBack"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "minFront"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "priority"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "source"),
        NodeInterface(NodeInterface::field, FieldValue::sfbool, "spatialize")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Sound>(*this, id));
    Vrml97NodeTypeImpl<Sound> & soundNodeType =
            static_cast<Vrml97NodeTypeImpl<Sound> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Sound>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            soundNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &Sound::processSet_direction,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Sound, SFVec3f>
                                    (&Sound::direction)));
        } else if (*itr == supportedInterfaces[1]) {
            soundNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &Sound::processSet_intensity,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Sound, SFFloat>
                                    (&Sound::intensity)));
        } else if (*itr == supportedInterfaces[2]) {
            soundNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &Sound::processSet_location,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Sound, SFVec3f>
                                    (&Sound::location)));
        } else if (*itr == supportedInterfaces[3]) {
            soundNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &Sound::processSet_maxBack,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Sound, SFFloat>
                                    (&Sound::maxBack)));
        } else if (*itr == supportedInterfaces[4]) {
            soundNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &Sound::processSet_maxFront,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Sound, SFFloat>
                                    (&Sound::maxFront)));
        } else if (*itr == supportedInterfaces[5]) {
            soundNodeType.addExposedField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                &Sound::processSet_minBack,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Sound, SFFloat>
                                    (&Sound::minBack)));
        } else if (*itr == supportedInterfaces[6]) {
            soundNodeType.addExposedField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                &Sound::processSet_minFront,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Sound, SFFloat>
                                    (&Sound::minFront)));
        } else if (*itr == supportedInterfaces[7]) {
            soundNodeType.addExposedField(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                &Sound::processSet_priority,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Sound, SFFloat>
                                    (&Sound::priority)));
        } else if (*itr == supportedInterfaces[8]) {
            soundNodeType.addExposedField(
                supportedInterfaces[8].fieldType,
                supportedInterfaces[8].id,
                &Sound::processSet_source,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Sound, SFNode>
                                    (&Sound::source)));
        } else if (*itr == supportedInterfaces[9]) {
            soundNodeType.addField(
                supportedInterfaces[9].fieldType,
                supportedInterfaces[9].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Sound, SFBool>
                                    (&Sound::spatialize)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Sound
 *
 * @brief Represents Sound node instances.
 */

/**
 * @var Sound::SoundClass
 *
 * @brief Class object for Sound instances.
 */

/**
 * @var SFVec3f Sound::direction
 *
 * @brief direction exposedField.
 */

/**
 * @var SFFloat Sound::intensity
 *
 * @brief intensity exposedField.
 */

/**
 * @var SFVec3f Sound::location
 *
 * @brief location exposedField.
 */

/**
 * @var SFFloat Sound::maxBack
 *
 * @brief maxBack exposedField.
 */

/**
 * @var SFFloat Sound::maxFront
 *
 * @brief maxFront exposedField.
 */

/**
 * @var SFFloat Sound::minBack
 *
 * @brief minBack exposedField.
 */

/**
 * @var SFFloat Sound::minFront
 *
 * @brief minFront exposedField.
 */

/**
 * @var SFFloat Sound::priority
 *
 * @brief priority exposedField.
 */

/**
 * @var SFNode Sound::source
 *
 * @brief source exposedField.
 */

/**
 * @var SFBool Sound::spatialize
 *
 * @brief spatialize field.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the instance.
 * @param scope     the Scope associated with the instance.
 */
Sound::Sound(const NodeType & nodeType,
             const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope),
    direction(vec3f(0, 0, 1)),
    intensity(1),
    maxBack(10),
    maxFront(10),
    minBack(1),
    minFront(1),
    spatialize(true)
{}

/**
 * @brief Destructor.
 */
Sound::~Sound() throw ()
{}

/**
 * @brief Propagate the bvolume dirty flag from children to parents.
 *
 * @param path  stack of ancestor nodes.
 * @param flags 1 indicates normal modified flag, 2 indicates the
 *              bvolume dirty flag, 3 indicates both.
 */
void Sound::updateModified(NodePath & path, int flags)
{
    if (this->isModified()) { markPathModified(path, true); }
    path.push_front(this);
    if (this->source.value) { this->source.value->updateModified(path); }
    path.pop_front();
}

/**
 * @brief Render the node.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void Sound::render(Viewer & viewer, const VrmlRenderContext context)
{
    // If this clip has been modified, update the internal data
    if (this->source.value && this->source.value->isModified()) {
        this->source.value->render(viewer, context);
    }
}

/**
 * @brief set_direction eventIn handler.
 *
 * @param sfvec3f   an SFVec3f.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void Sound::processSet_direction(const FieldValue & sfvec3f,
                                 const double timestamp)
    throw (std::bad_cast)
{
    this->direction = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("direction_changed", this->direction, timestamp);
}

/**
 * @brief set_intensity eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void Sound::processSet_intensity(const FieldValue & sffloat,
                                 const double timestamp)
    throw (std::bad_cast)
{
    this->intensity = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("intensity_changed", this->intensity, timestamp);
}

/**
 * @brief set_location eventIn handler.
 *
 * @param sfvec3f   an SFVec3f.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void Sound::processSet_location(const FieldValue & sfvec3f,
                                const double timestamp)
    throw (std::bad_cast)
{
    this->location = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("location_changed", this->location, timestamp);
}

/**
 * @brief set_maxBack eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void Sound::processSet_maxBack(const FieldValue & sffloat,
                               const double timestamp)
    throw (std::bad_cast)
{
    this->maxBack = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("maxBack_changed", this->maxBack, timestamp);
}

/**
 * @brief set_maxFront eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void Sound::processSet_maxFront(const FieldValue & sffloat,
                                const double timestamp)
    throw (std::bad_cast)
{
    this->maxFront = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("maxFront_changed", this->maxFront, timestamp);
}

/**
 * @brief set_minBack eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void Sound::processSet_minBack(const FieldValue & sffloat,
                               const double timestamp)
    throw (std::bad_cast)
{
    this->minBack = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("minBack_changed", this->minBack, timestamp);
}

/**
 * @brief set_minFront eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void Sound::processSet_minFront(const FieldValue & sffloat,
                                const double timestamp)
    throw (std::bad_cast)
{
    this->minFront = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("minFront_changed", this->minFront, timestamp);
}

/**
 * @brief set_priority eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void Sound::processSet_priority(const FieldValue & sffloat,
                                const double timestamp)
    throw (std::bad_cast)
{
    this->priority = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("priority_changed", this->priority, timestamp);
}

/**
 * @brief set_source eventIn handler.
 *
 * @param sfnode    an SFNode.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfnode is not an SFNode.
 */
void Sound::processSet_source(const FieldValue & sfnode, double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->source = dynamic_cast<const SFNode &>(sfnode);
    this->setModified();
    this->emitEvent("source_changed", this->source, timestamp);
}


/**
 * @class SphereClass
 *
 * @brief Class object for @link Sphere Sphere@endlink.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
SphereClass::SphereClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
SphereClass::~SphereClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Sphere nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by SphereClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr SphereClass::createType(const std::string & id,
                                          const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterface =
            NodeInterface(NodeInterface::field, FieldValue::sffloat, "radius");
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Sphere>(*this, id));
    Vrml97NodeTypeImpl<Sphere> & spereNodeType =
            static_cast<Vrml97NodeTypeImpl<Sphere> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Sphere>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterface) {
            spereNodeType.addField(
                supportedInterface.fieldType,
                supportedInterface.id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Sphere, SFFloat>
                                    (&Sphere::radius)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Sphere
 *
 * @brief Sphere node instances.
 */

/**
 * @var Sphere::SphereClass
 *
 * @brief Class object for Sphere instances.
 */

/**
 * @var SFFloat Sphere::radius
 *
 * @brief radius field.
 */

/**
 * @var BSphere Sphere::bsphere
 *
 * @brief Bounding volume.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
Sphere::Sphere(const NodeType & nodeType,
               const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractGeometry(nodeType, scope),
    radius(1.0)
{
    this->setBVolumeDirty(true); // lazy calc of bvolumes
}

/**
 * @brief Destructor.
 */
Sphere::~Sphere() throw ()
{}

/**
 * @brief Insert this geometry into @p viewer's display list.
 *
 * @param viewer    a Viewer.
 * @param context   the rendering context.
 */
Viewer::Object Sphere::insertGeometry(Viewer & viewer,
                                      const VrmlRenderContext context)
{
    return viewer.insertSphere(this->radius.value);
}

/**
 * @brief Get the bounding volume.
 *
 * @return the bounding volume associated with the node.
 */
const BVolume * Sphere::getBVolume() const
{
    if (this->isBVolumeDirty()) {
        ((Sphere*)this)->bsphere.setRadius(this->radius.value);
        ((Node*)this)->setBVolumeDirty(false); // logical const
    }
    return &this->bsphere;
}


/**
 * @class SphereSensorClass
 *
 * @brief Class object for @link SphereSensor SphereSensor@endlink.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
SphereSensorClass::SphereSensorClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
SphereSensorClass::~SphereSensorClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating SphereSensor nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by SphereSensorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
SphereSensorClass::createType(const std::string & id,
                              const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "autoOffset"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "enabled"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "offset"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isActive"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfrotation, "rotation_changed"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfvec3f, "trackPoint_changed")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<SphereSensor>(*this, id));
    Vrml97NodeTypeImpl<SphereSensor> & sphereSensorNodeType =
            static_cast<Vrml97NodeTypeImpl<SphereSensor> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<SphereSensor>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            sphereSensorNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &SphereSensor::processSet_autoOffset,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SphereSensor, SFBool>
                                    (&SphereSensor::autoOffset)));
        } else if (*itr == supportedInterfaces[1]) {
            sphereSensorNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &SphereSensor::processSet_enabled,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SphereSensor, SFBool>
                                    (&SphereSensor::enabled)));
        } else if (*itr == supportedInterfaces[2]) {
            sphereSensorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &SphereSensor::processSet_offset,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SphereSensor, SFRotation>
                                    (&SphereSensor::offset)));
        } else if (*itr == supportedInterfaces[3]) {
            sphereSensorNodeType.addEventOut(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SphereSensor, SFBool>
                                    (&SphereSensor::active)));
        } else if (*itr == supportedInterfaces[4]) {
            sphereSensorNodeType.addEventOut(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SphereSensor, SFRotation>
                                    (&SphereSensor::rotation)));
        } else if (*itr == supportedInterfaces[5]) {
            sphereSensorNodeType.addEventOut(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SphereSensor, SFVec3f>
                                    (&SphereSensor::trackPoint)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class SphereSensor
 *
 * @brief SphereSensor node instances.
 */

/**
 * @var SphereSensor::SphereSensorClass
 *
 * @brief Class object for SphereSensor instances.
 */

/**
 * @var SFBool SphereSensor::autoOffset
 *
 * @brief autoOffset exposedField.
 */

/**
 * @var SFBool SphereSensor::enabled
 *
 * @brief enabled exposedField.
 */

/**
 * @var SFRotation SphereSensor::offset
 *
 * @brief offset exposedField.
 */

/**
 * @var SFBool SphereSensor::active
 *
 * @brief isActive eventOut.
 */

/**
 * @var SFRotation SphereSensor::rotation
 *
 * @brief rotation_changed eventOut.
 */

/**
 * @var SFVec3f SphereSensor::trackPoint
 *
 * @brief trackPoint_changed eventOut.
 */

/**
 * @var SFVec3f SphereSensor::activationPoint
 *
 * @brief The start point of a drag operation.
 */

/**
 * @var SFVec3f SphereSensor::centerPoint
 *
 * @brief Center point.
 */

/**
 * @var mat4f SphereSensor::modelview
 *
 * @brief Modelview matrix.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
SphereSensor::SphereSensor(const NodeType & nodeType,
                           const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope),
    autoOffset(true),
    enabled(true),
    offset(OpenVRML::rotation(0.0, 1.0, 0.0, 0.0)),
    active(false)
{
    this->setModified();
}

/**
 * @brief Destructor.
 */
SphereSensor::~SphereSensor() throw ()
{}

/**
 * @brief Cast to a SphereSensor.
 *
 * @return a pointer to the node.
 */
SphereSensor * SphereSensor::toSphereSensor() const
{
    return const_cast<SphereSensor *>(this);
}

/**
 * @brief Render the node.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void SphereSensor::render(Viewer & viewer, const VrmlRenderContext context)
{
    //
    // Store the ModelView matrix which is calculated at the time of rendering
    // in render-context. This matrix will be in use at the time of activation
    //
    this->modelview = context.getMatrix();
}

/**
 * @brief Activate or deactivate the SphereSensor.
 *
 * Activating a drag sensor means that the pointing device button has been
 * depressed and a drag operation has been initiated. The sensor is deactivated
 * when the button is released at the end of the operation.
 *
 * @param timeStamp the current time.
 * @param isActive  @c true if the drag operation is in progress; @c false
 *                  otherwise.
 * @param p         the pointing device position.
 */
void SphereSensor::activate(double timeStamp, bool isActive, double * p)
{
    // Become active
    if (isActive && !this->active.value) {
        this->active.value = isActive;

        // set activation point in world coords
        const vec3f floatVec(p[0], p[1], p[2]);
        this->activationPoint.value = floatVec;

        if (this->autoOffset.value) { this->rotation = this->offset; }

        // calculate the center of the object in world coords
        vec3f V;
        mat4f M = this->modelview.inverse();
        V = V * M;
        this->centerPoint.value = V;

        // send message
        this->emitEvent("isActive", this->active, timeStamp);
    }
    // Become inactive
    else if (!isActive && this->active.value) {
        this->active.value = isActive;
        this->emitEvent("isActive", this->active, timeStamp);

        // save auto offset of rotation
        if (this->autoOffset.value) {
            this->offset = this->rotation;
            this->emitEvent("offset_changed", this->offset, timeStamp);
        }
    }
    // Tracking
    else if (isActive) {
        // get local coord for touch point
        vec3f V(p[0], p[1], p[2]);
        mat4f M = this->modelview.inverse();
        V = V * M;
        this->trackPoint.value = V;
        this->emitEvent("trackPoint_changed", this->trackPoint, timeStamp);

        vec3f V2(p[0], p[1], p[2]);
        vec3f tempv = V2 - this->centerPoint.value;
        vec3f dir1(tempv);
        double dist = dir1.length();                // get the length of the pre-normalized vector
        dir1 = dir1.normalize();
        tempv = this->activationPoint.value - this->centerPoint.value;
        vec3f dir2(tempv);
        dir2 = dir2.normalize();

        tempv = dir1 * dir2;
        vec3f cx(tempv);
        cx = cx.normalize();

        OpenVRML::rotation newRot(cx, dist * acos(dir1.dot(dir2)));
        if (this->autoOffset.value) {
            newRot = newRot * this->offset.value;
        }
        this->rotation.value = newRot;

        this->emitEvent("rotation_changed", this->rotation, timeStamp);
    }
}

/**
 * @brief Determine whether the SphereSensor is enabled.
 *
 * @return @c true if the SphereSensor is enabled; @c false otherwise.
 */
bool SphereSensor::isEnabled() const throw ()
{
    return this->enabled.value;
}

/**
 * @brief set_autoOffset eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void SphereSensor::processSet_autoOffset(const FieldValue & sfbool,
                                         const double timestamp)
    throw (std::bad_cast)
{
    this->autoOffset = dynamic_cast<const SFBool &>(sfbool);
    this->emitEvent("autoOffset_changed", this->autoOffset, timestamp);
}

/**
 * @brief set_enabled eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void SphereSensor::processSet_enabled(const FieldValue & sfbool,
                                      const double timestamp)
    throw (std::bad_cast)
{
    this->enabled = dynamic_cast<const SFBool &>(sfbool);
    this->emitEvent("enabled_changed", this->enabled, timestamp);
}

/**
 * @brief set_offset eventIn handler.
 *
 * @param sfrotation    an SFRotation value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfrotation is not an SFRotation.
 */
void SphereSensor::processSet_offset(const FieldValue & sfrotation,
                                     const double timestamp)
    throw (std::bad_cast)
{
    this->offset = dynamic_cast<const SFRotation &>(sfrotation);
    this->emitEvent("offset_changed", this->offset, timestamp);
}


/**
 * @class SpotLightClass
 *
 * @brief Class object for @link SpotLight SpotLight@endlink.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
SpotLightClass::SpotLightClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
SpotLightClass::~SpotLightClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating PointLight nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by PointLightClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
SpotLightClass::createType(const std::string & id,
                           const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "ambientIntensity"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "attenuation"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "beamWidth"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfcolor, "color"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "cutOffAngle"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "direction"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "intensity"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "location"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "on"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "radius")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<SpotLight>(*this, id));
    Vrml97NodeTypeImpl<SpotLight> & spotLightNodeType =
            static_cast<Vrml97NodeTypeImpl<SpotLight> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<SpotLight>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            spotLightNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &SpotLight::processSet_ambientIntensity,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SpotLight, SFFloat>
                                    (&SpotLight::ambientIntensity)));
        } else if (*itr == supportedInterfaces[1]) {
            spotLightNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &SpotLight::processSet_attenuation,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SpotLight, SFVec3f>
                                    (&SpotLight::attenuation)));
        } else if (*itr == supportedInterfaces[2]) {
            spotLightNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &SpotLight::processSet_beamWidth,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SpotLight, SFFloat>
                                    (&SpotLight::beamWidth)));
        } else if (*itr == supportedInterfaces[3]) {
            spotLightNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &SpotLight::processSet_color,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SpotLight, SFColor>
                                    (&SpotLight::color)));
        } else if (*itr == supportedInterfaces[4]) {
            spotLightNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &SpotLight::processSet_cutOffAngle,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SpotLight, SFFloat>
                                    (&SpotLight::cutOffAngle)));
        } else if (*itr == supportedInterfaces[5]) {
            spotLightNodeType.addExposedField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                &SpotLight::processSet_direction,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SpotLight, SFVec3f>
                                    (&SpotLight::direction)));
        } else if (*itr == supportedInterfaces[6]) {
            spotLightNodeType.addExposedField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                &SpotLight::processSet_intensity,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SpotLight, SFFloat>
                                    (&SpotLight::intensity)));
        } else if (*itr == supportedInterfaces[7]) {
            spotLightNodeType.addExposedField(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                &SpotLight::processSet_location,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SpotLight, SFVec3f>
                                    (&SpotLight::location)));
        } else if (*itr == supportedInterfaces[8]) {
            spotLightNodeType.addExposedField(
                supportedInterfaces[8].fieldType,
                supportedInterfaces[8].id,
                &SpotLight::processSet_on,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SpotLight, SFBool>
                                    (&SpotLight::on)));
        } else if (*itr == supportedInterfaces[9]) {
            spotLightNodeType.addExposedField(
                supportedInterfaces[9].fieldType,
                supportedInterfaces[9].id,
                &SpotLight::processSet_radius,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<SpotLight, SFFloat>
                                    (&SpotLight::radius)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class SpotLight
 *
 * @brief SpotLight node instances.
 */

/**
 * @var SpotLight::SpotLightClass
 *
 * @brief Class object for SpotLight instances.
 */

/**
 * @var SFVec3f SpotLight::attenuation
 *
 * @brief attenuation exposedField.
 */

/**
 * @var SFFloat SpotLight::beamWidth
 *
 * @brief beamWidth exposedField.
 */

/**
 * @var SFFloat SpotLight::cutOffAngle
 *
 * @brief cutOffAngle exposedField.
 */

/**
 * @var SFVec3f SpotLight::direction
 *
 * @brief direction exposedField.
 */

/**
 * @var SFVec3f SpotLight::location
 *
 * @brief location exposedField.
 */

/**
 * @var SFFloat SpotLight::radius
 *
 * @brief radius exposedField.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
SpotLight::SpotLight(const NodeType & nodeType,
                     const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractLight(nodeType, scope),
    attenuation(vec3f(1.0, 0.0, 0.0)),
    beamWidth(1.570796),
    cutOffAngle(0.785398),
    direction(vec3f(0.0, 0.0, -1.0)),
    location(vec3f(0.0, 0.0, 0.0)),
    radius(100)
{}

/**
 * @brief Destructor.
 */
SpotLight::~SpotLight() throw ()
{}

/**
 * @brief Cast to a SpotLight.
 *
 * @return a pointer to the node.
 */
SpotLight * SpotLight::toSpotLight() const
{
    return const_cast<SpotLight *>(this);
}

/**
 * @brief Render the scoped light.
 *
 * @param viewer    a Viewer.
 *
 * @todo This should be called before rendering any geometry in the scene.
 *      Since this is called from Scene::render before traversing the
 *      scene graph, the proper transformation matrix hasn't been set up.
 *      Somehow it needs to figure out the accumulated xforms of its
 *      parents and apply them before rendering. This is not easy with
 *      DEF/USEd nodes...
 */
void SpotLight::renderScoped(Viewer & viewer)
{
    if (this->on.value && this->radius.value > 0.0) {
        viewer.insertSpotLight(this->ambientIntensity.value,
                               this->attenuation.value,
                               this->beamWidth.value,
                               this->color.value,
                               this->cutOffAngle.value,
                               this->direction.value,
                               this->intensity.value,
                               this->location.value,
                               this->radius.value);
    }
    this->clearModified();
}

/**
 * @brief Initialize.
 *
 * @param timestamp the current time.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void SpotLight::do_initialize(const double timestamp) throw (std::bad_alloc)
{
    assert(this->getScene());
    this->getScene()->browser.addScopedLight(*this);
}

/**
 * @brief Shut down.
 *
 * @param timestamp the current time.
 */
void SpotLight::do_shutdown(const double timestamp) throw ()
{
    assert(this->getScene());
    this->getScene()->browser.removeScopedLight(*this);
}

/**
 * @brief set_attenuation eventIn handler.
 *
 * @param sfvec3f   an SFVec3f.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void SpotLight::processSet_attenuation(const FieldValue & sfvec3f,
                                       const double timestamp)
    throw (std::bad_cast)
{
    this->attenuation = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("attenuation_changed", this->attenuation, timestamp);
}

/**
 * @brief set_beamWidth eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void SpotLight::processSet_beamWidth(const FieldValue & sffloat,
                                     const double timestamp)
    throw (std::bad_cast)
{
    this->beamWidth = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("beamWidth_changed", this->beamWidth, timestamp);
}

/**
 * @brief set_cutOffAngle eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void SpotLight::processSet_cutOffAngle(const FieldValue & sffloat,
                                       const double timestamp)
    throw (std::bad_cast)
{
    this->cutOffAngle = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("cutOffAngle_changed", this->cutOffAngle, timestamp);
}

/**
 * @brief set_direction eventIn handler.
 *
 * @param sfvec3f   an SFVec3f.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void SpotLight::processSet_direction(const FieldValue & sfvec3f,
                                     const double timestamp)
    throw (std::bad_cast)
{
    this->direction = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("direction_changed", this->direction, timestamp);
}

/**
 * @brief set_location eventIn handler.
 *
 * @param sfvec3f   an SFVec3f.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void SpotLight::processSet_location(const FieldValue & sfvec3f,
                                    const double timestamp)
    throw (std::bad_cast)
{
    this->location = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("location_changed", this->location, timestamp);
}

/**
 * @brief set_radius eventIn handler.
 *
 * @param sffloat   an SFFloat.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void SpotLight::processSet_radius(const FieldValue & sffloat,
                                  const double timestamp)
    throw (std::bad_cast)
{
    this->radius = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("radius_changed", this->radius, timestamp);
}


/**
 * @class SwitchClass
 *
 * @brief Class object for @link Switch Switch@endlink.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
SwitchClass::SwitchClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
SwitchClass::~SwitchClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Switch nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by SwitchClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr SwitchClass::createType(const std::string & id,
                                          const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::mfnode, "choice"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfint32, "whichChoice")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Switch>(*this, id));
    Vrml97NodeTypeImpl<Switch> & switchNodeType =
            static_cast<Vrml97NodeTypeImpl<Switch> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Switch>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            switchNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &Switch::processSet_choice,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Switch, MFNode>
                                    (&Switch::choice)));
        } else if (*itr == supportedInterfaces[1]) {
            switchNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &Switch::processSet_whichChoice,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Switch, SFInt32>
                                    (&Switch::whichChoice)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Switch
 *
 * @brief Switch node instances.
 */

/**
 * @var Switch::SwitchClass
 *
 * @brief Class object for Switch instances.
 */

/**
 * @var MFNode Switch::choice
 *
 * @brief choice exposedField.
 */

/**
 * @var SFInt32 Switch::whichChoice
 *
 * @brief whichChoice exposedField.
 */

/**
 * @var MFNode Switch::children
 *
 * @brief The children currently in the scene graph.
 */

/**
 * @var BSphere Switch::bsphere
 *
 * @brief Cached copy of the bsphere enclosing this node's children.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType  the NodeType associated with the node instance.
 * @param scope     the Scope to which the node belongs.
 */
Switch::Switch(const NodeType & nodeType,
               const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    ChildNode(nodeType, scope),
    GroupingNode(nodeType, scope),
    whichChoice(-1),
    children(1)
{
    this->setBVolumeDirty(true);
}

/**
 * @brief Destructor.
 */
Switch::~Switch() throw () {}

/**
 * @brief Determine whether the node has been modified.
 *
 * @return @c true if the node or one of its children has been modified,
 *      @c false otherwise.
 */
bool Switch::isModified() const {
    if (d_modified) { return true; }

    long w = this->whichChoice.value;

    return (w >= 0 && size_t(w) < this->choice.value.size()
            && this->choice.value[w]->isModified());
}

/**
 * @brief Propagate the bvolume dirty flag from children to parents.
 *
 * @param path  stack of ancestor nodes.
 * @param flags 1 indicates normal modified flag, 2 indicates the
 *              bvolume dirty flag, 3 indicates both.
 */
void Switch::updateModified(NodePath & path, int flags) {
    //
    // ok: again we get this issue of whether to check _all_ the children
    // or just the current choice (ref LOD). again, chooise to test them
    // all. note that the original isModified() just tested the current
    // one. keep that in mind, and change it back when confirmed safe.
    //
    if (this->isModified()) { markPathModified(path, true); }
    path.push_front(this);
    for (size_t i = 0; i < this->choice.value.size(); ++i) {
        this->choice.value[i]->updateModified(path);
    }
    path.pop_front();
}

/**
 * @brief Render the node.
 *
 * The child corresponding to @a whichChoice is rendered. Nothing is rendered if
 * @a whichChoice is -1.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void Switch::render(Viewer & viewer, const VrmlRenderContext context)
{
    if (this->children.value[0]) {
        this->children.value[0]->render(viewer, context);
    }
    this->clearModified();
}

/**
 * @brief Get the bounding volume.
 *
 * @return the bounding volume associated with the node.
 */
const BVolume* Switch::getBVolume() const {
    if (this->isBVolumeDirty()) {
        ((Switch*)this)->recalcBSphere();
    }
    return &this->bsphere;
}

/**
 * @brief Get the children in the scene graph.
 *
 * @return the child nodes in the scene graph.
 */
const std::vector<NodePtr> & Switch::getChildren() const throw ()
{
    return this->children.value;
}

/**
 * Pass on to enabled touchsensor child.
 */
void Switch::activate(double time, bool isOver, bool isActive, double *p)
{
    const std::vector<NodePtr> & children = this->getChildren();
    const NodePtr & node = children[0];
    if (node) {
        if (node->toTouchSensor() && node->toTouchSensor()->isEnabled()) {
            node->toTouchSensor()->activate(time, isOver, isActive, p);
        } else if (node->toPlaneSensor()
                && node->toPlaneSensor()->isEnabled()) {
            node->toPlaneSensor()->activate(time, isActive, p);
        } else if (node->toCylinderSensor()
                && node->toCylinderSensor()->isEnabled()) {
            node->toCylinderSensor()->activate(time, isActive, p);
        } else if (node->toSphereSensor()
                && node->toSphereSensor()->isEnabled()) {
            node->toSphereSensor()->activate(time, isActive, p);
        }
    }
}

/**
 * @brief Recalculate the bounding volume.
 */
void Switch::recalcBSphere() {
    this->bsphere.reset();
    long w = this->whichChoice.value;
    if (w >= 0 && size_t(w) < this->choice.value.size()) {
        const NodePtr & node = this->choice.value[w];
        if (node) {
            const BVolume * ci_bv = node->getBVolume();
            if (ci_bv) { this->bsphere.extend(*ci_bv); }
        }
    }
    this->setBVolumeDirty(false);
}

/**
 * @brief set_choice eventIn handler.
 *
 * @param mfnode    an MFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfnode is not an MFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Switch::processSet_choice(const FieldValue & mfnode,
                               const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->choice = dynamic_cast<const MFNode &>(mfnode);
    const size_t whichChoice = size_t(this->whichChoice.value);
    this->children.value[0] = (whichChoice < this->choice.value.size())
                            ? this->choice.value[whichChoice]
                            : NodePtr(0);
    this->setModified();
    this->emitEvent("choice_changed", this->choice, timestamp);
}

/**
 * @brief set_whichChoice eventIn handler.
 *
 * @param sfint32   an SFInt32 value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfint32 is not an SFInt32.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Switch::processSet_whichChoice(const FieldValue & sfint32,
                                    const double timestamp)
    throw (std::bad_cast)
{
    this->whichChoice = dynamic_cast<const SFInt32 &>(sfint32);
    const size_t whichChoice = size_t(this->whichChoice.value);
    this->children.value[0] = (whichChoice < this->choice.value.size())
                            ? this->choice.value[whichChoice]
                            : NodePtr(0);
    this->setModified();
    this->emitEvent("whichChoice_changed", this->whichChoice, timestamp);
}


/**
 * @class TextClass
 *
 * @brief Class object for Text nodes.
 */

/**
 * @var FT_Library TextClass::freeTypeLibrary
 *
 * @brief FreeType library handle.
 *
 * @see http://freetype.org/freetype2/docs/reference/ft2-base_interface.html#FT_Library
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
TextClass::TextClass(Browser & browser):
    NodeClass(browser)
{
# if OPENVRML_ENABLE_TEXT_NODE
    FT_Error error = 0;
    error = FT_Init_FreeType(&this->freeTypeLibrary);
    if (error) {
        browser.err << "Error initializing FreeType library." << std::endl;
    }
# endif // OPENVRML_ENABLE_TEXT_NODE
}

/**
 * @brief Destructor.
 */
TextClass::~TextClass() throw ()
{
# if OPENVRML_ENABLE_TEXT_NODE
    FT_Error error = 0;
    error = FT_Done_FreeType(this->freeTypeLibrary);
    if (error) {
        browser.err << "Error shutting down FreeType library." << std::endl;
    }
# endif // OPENVRML_ENABLE_TEXT_NODE
}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Text nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by TextClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr TextClass::createType(const std::string & id,
                                        const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::mfstring, "string"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfnode, "fontStyle"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mffloat, "length"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "maxExtent")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Text>(*this, id));
    Vrml97NodeTypeImpl<Text> & textNodeType =
            static_cast<Vrml97NodeTypeImpl<Text> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Text>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            textNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &Text::processSet_string,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Text, MFString>
                                    (&Text::string)));
        } else if (*itr == supportedInterfaces[1]) {
            textNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &Text::processSet_fontStyle,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Text, SFNode>
                                    (&Text::fontStyle)));
        } else if (*itr == supportedInterfaces[2]) {
            textNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &Text::processSet_length,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Text, MFFloat>
                                    (&Text::length)));
        } else if (*itr == supportedInterfaces[3]) {
            textNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &Text::processSet_maxExtent,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Text, SFFloat>
                                    (&Text::maxExtent)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Text
 *
 * @brief Text node instances.
 */

/**
 * @var Text::TextClass
 *
 * @brief Class object for Text instances.
 */

/**
 * @var MFString Text::string
 *
 * @brief string exposedField.
 */

/**
 * @var SFNode Text::fontStyle
 *
 * @brief fontStyle exposedField.
 */

/**
 * @var MFFloat Text::length
 *
 * @brief length exposedField.
 */

/**
 * @var SFFloat Text::maxExtent
 *
 * @brief maxExtent exposedField.
 */

/**
 * @struct Text::GlyphGeometry
 *
 * @brief Used to hold the geometry of individual glyphs.
 */

/**
 * @var MFVec2f Text::GlyphGeometry::coord
 *
 * @brief Glyph coordinates.
 */

/**
 * @var MFInt32 Text::GlyphGeometry::coordIndex
 *
 * @brief Glyph coordinate indices.
 */

/**
 * @var float Text::GlyphGeometry::advanceWidth
 *
 * @brief The distance the pen should advance horizontally after drawing the
 *      glyph.
 */

/**
 * @var float Text::GlyphGeometry::advanceHeight
 *
 * @brief The distance the pen should advance vertically after drawing the
 *      glyph.
 */

# ifdef OPENVRML_ENABLE_TEXT_NODE
namespace {

    const vec2f * getClosestVertex_(const MFVec2f & contour,
                                    const vec2f & point)
        throw ()
    {
        assert(contour.value.size() > 1);
        const vec2f * result = 0;
        float shortestDistance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < contour.value.size(); ++i) {
            const vec2f & element = contour.value[i];
            const float x = point[0] - element.x();
            const float y = point[1] - element.y();
            const float distance = sqrt(x * x + y * y);
            if (distance < shortestDistance) {
                shortestDistance = distance;
                result = &element;
            }
        }
        assert(result);
        return result;
    }

    bool insideContour_(const MFVec2f & contour, const vec2f & point)
        throw ()
    {
        bool result = false;
        const size_t nvert = contour.value.size();
        for (size_t i = 0, j = nvert - 1; i < nvert; j = i++) {
            const vec2f & vi = contour.value[i];
            const vec2f & vj = contour.value[j];
            if ((((vi.y() <= point.y()) && (point.y() < vj.y()))
                        || ((vj.y() <= point.y()) && (point.y() < vi.y())))
                    && (point.x() < (vj.x() - vi.x())
                        * (point.y() - vi.y()) / (vj.y() - vi.y()) + vi.x())) {
                result = !result;
            }
        }
        return result;
    }

    enum ContourType_ { exterior_, interior_ };

    ContourType_ getType(const MFVec2f & contour,
                         const std::vector<MFVec2f> & contours)
        throw ()
    {
        using std::vector;

        assert(contour.value.size() > 0);
        const vec2f & vertex = contour.value[0];

        bool isInterior = false;
        for (vector<MFVec2f>::const_iterator testContour = contours.begin();
                testContour != contours.end(); ++testContour) {
            if (&*testContour == &contour) { continue; }
            if (insideContour_(*testContour, vertex)) {
                isInterior = !isInterior;
            }
        }
        return isInterior ? interior_ : exterior_;
    }

    struct Polygon_ {
        const MFVec2f * exterior;
        std::vector<const MFVec2f *> interiors;
    };

    struct Inside_ : std::binary_function {
        bool operator()(const MFVec2f * const lhs,
                        const MFVec2f * const rhs) const
        {
            assert(lhs);
            assert(rhs);
            assert(!lhs->value.empty());
            //
            // Assume contours don't intersect. So if one point on lhs is
            // inside rhs, then assume all of lhs is inside rhs.
            //
            return insideContour_(*rhs, lhs->value[0]);
        }
    };

    const std::vector<Polygon_>
    getPolygons_(const std::vector<MFVec2f> & contours)
        throw (std::bad_alloc)
    {
        using std::vector;
        typedef std::multiset<const MFVec2f *, Inside_> Contours;

        //
        // First, divide the contours into interior and exterior contours.
        //
        Contours interiors, exteriors;
        for (vector<MFVec2f>::const_iterator contour = contours.begin();
                contour != contours.end(); ++contour) {
            switch (getType(*contour, contours)) {
            case interior_:
                interiors.insert(&*contour);
                break;
            case exterior_:
                exteriors.insert(&*contour);
                break;
            default:
                assert(false);
            }
        }

        //
        // For each exterior, find its associated interiors and group them in
        // a Polygon_.
        //
        vector<Polygon_> polygons;
        while (!exteriors.empty()) {
            Polygon_ polygon;
            polygon.exterior = *exteriors.begin();
            Contours::iterator interior = interiors.begin();
            while (interior != interiors.end()) {
                assert(!(*interior)->value.empty());
                if (insideContour_(*polygon.exterior, (*interior)->value[0])) {
                    polygon.interiors.push_back(*interior);
                    Contours::iterator next = interior;
                    ++next;
                    interiors.erase(interior);
                    interior = next;
                } else {
                    ++interior;
                }
            }
            polygons.push_back(polygon);
            exteriors.erase(exteriors.begin());
        }
        return polygons;
    }

    long getVertexIndex_(const MFVec2f & vertices, const vec2f & vertex)
        throw ()
    {
        using OpenVRML_::fpequal;
        for (size_t i = 0; i < vertices.value.size(); ++i) {
            const vec2f & element = vertices.value[i];
            if (fpequal(vertex[0], element[0])
                    && fpequal(vertex[1], element[1])) {
                return i;
            }
        }
        return -1;
    }
}
# endif // OPENVRML_ENABLE_TEXT_NODE

/**
 * @brief Construct from a set of contours.
 *
 * @param contours      a vector of closed contours that make up the glyph's
 *                      outline.
 * @param advanceWidth  the distance the pen should advance horizontally after
 *                      drawing the glyph.
 * @param advanceHeight the distance the pen should advance vertically after
 *                      drawing the glyph.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
Text::GlyphGeometry::GlyphGeometry(const std::vector<MFVec2f> & contours,
                                   const float advanceWidth,
                                   const float advanceHeight)
    throw (std::bad_alloc):
    advanceWidth(advanceWidth),
    advanceHeight(advanceHeight)
{
# ifdef OPENVRML_ENABLE_TEXT_NODE
    using std::vector;

    const vector<Polygon_> polygons = getPolygons_(contours);
    for (vector<Polygon_>::const_iterator polygon = polygons.begin();
            polygon != polygons.end(); ++polygon) {
        //
        // connectionMap is keyed on a pointer to a vertex on the exterior
        // contour, and maps to a pointer to the interior contour whose
        // first vertex is closest to the exterior vertex.
        //
        typedef std::multimap<const vec2f *, const MFVec2f *> ConnectionMap;
        ConnectionMap connectionMap;

        //
        // Fill connectionMap. For each interior contour, find the exterior
        // vertex that is closest to the first vertex in the interior contour,
        // and the put the pair in the map.
        //
        for (vector<const MFVec2f *>::const_iterator interior =
                polygon->interiors.begin();
                interior != polygon->interiors.end();
                ++interior) {
            assert(*interior);
            assert(!(*interior)->value.empty());
            const vec2f * const exteriorVertex =
                    getClosestVertex_(*polygon->exterior,
                                      (*interior)->value[0]);
            assert(exteriorVertex);
            const ConnectionMap::value_type value(exteriorVertex, *interior);
            connectionMap.insert(value);
        }

        //
        // Finally, draw the polygon.
        //
        assert(!polygon->exterior->value.empty());
        for (size_t i = 0; i < polygon->exterior->value.size(); ++i) {
            const vec2f & exteriorVertex = polygon->exterior->value[i];
            long exteriorIndex = getVertexIndex_(this->coord, exteriorVertex);
            if (exteriorIndex > -1) {
                this->coordIndex.value.push_back(exteriorIndex);
            } else {
                this->coord.value.push_back(exteriorVertex);
                assert(!this->coord.value.empty());
                exteriorIndex = this->coord.value.size() - 1;
                this->coordIndex.value.push_back(exteriorIndex);
            }
            ConnectionMap::iterator pos;
            while ((pos = connectionMap.find(&exteriorVertex))
                    != connectionMap.end()) {
                for (int i = pos->second->value.size() - 1; i > -1; --i) {
                    const vec2f & interiorVertex = pos->second->value[i];
                    const long interiorIndex = getVertexIndex_(this->coord,
                                                               interiorVertex);
                    if (interiorIndex > -1) {
                        this->coordIndex.value.push_back(interiorIndex);
                    } else {
                        this->coord.value.push_back(interiorVertex);
                        assert(!this->coord.value.empty());
                        this->coordIndex
                            .value.push_back(this->coord.value.size() - 1);
                    }
                }
                this->coordIndex.value.push_back(exteriorIndex);
                connectionMap.erase(pos);
            }
        }
        assert(connectionMap.empty());
        this->coordIndex.value.push_back(-1);
    }
# endif // OPENVRML_ENABLE_TEXT_NODE
}

/**
 * @struct Text::TextGeometry
 *
 * @brief Holds the text geometry.
 */

/**
 * @var MFVec3f Text::TextGeometry::coord
 *
 * @brief Text geometry coordinates.
 */

/**
 * @var MFInt32 Text::TextGeometry::coordIndex
 *
 * @brief Text geometry coordinate indices.
 */

/**
 * @var MFVec3f Text::TextGeometry::normal
 *
 * @brief Text geometry normals.
 */

/**
 * @typedef Text::Ucs4String
 *
 * @brief A vector of FcChar32 vectors.
 */

/**
 * @typedef Text::GlyphGeometryMap
 *
 * @brief Maps FT_UInts to GlyphGeometry.
 *
 * @see http://freetype.org/freetype2/docs/reference/ft2-basic_types.html#FT_UInt
 */

/**
 * @var Text::Ucs4String Text::ucs4String
 *
 * @brief UCS-4 equivalent of the (UTF-8) data in @a string.
 */

/**
 * @var FT_Face Text::face
 *
 * @brief Handle to the font face.
 *
 * @see http://freetype.org/freetype2/docs/reference/ft2-base_interface.html#FT_Face
 */

/**
 * @var Text::GlyphGeometryMap Text::glyphGeometryMap
 *
 * @brief Map of glyph indices to GlyphGeometry.
 *
 * GlyphGeometry instances are created as needed, as new glyphs are encountered.
 * Once they are created, they are cached in the glyphGeometryMap for rapid
 * retrieval the next time the glyph is encountered.
 */

/**
 * @var Text::TextGeometry Text::textGeometry
 *
 * @brief The text geometry.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType      the NodeType associated with the instance.
 * @param scope         the Scope that the new node will belong to.
 */
Text::Text(const NodeType & nodeType,
           const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractGeometry(nodeType, scope),
    face(0)
{}

/**
 * @brief Destructor.
 */
Text::~Text() throw ()
{}

/**
 * @brief Determine whether the node has been modified.
 *
 * @return @c true if the node or one of its children has been modified,
 *      @c false otherwise.
 */
bool Text::isModified() const {
    return (this->Node::isModified()
            || (this->fontStyle.value && this->fontStyle.value->isModified()));
}

/**
 * @brief Propagate the bvolume dirty flag from children to parents.
 *
 * @param path  stack of ancestor nodes.
 * @param flags 1 indicates normal modified flag, 2 indicates the
 *              bvolume dirty flag, 3 indicates both.
 */
void Text::updateModified(NodePath & path, int flags) {
    if (this->isModified()) { markPathModified(path, true); }
    path.push_front(this);
    if (this->fontStyle.value) { this->fontStyle.value->updateModified(path); }
    path.pop_front();
}

/**
 * @brief Insert this geometry into @p viewer's display list.
 *
 * @param viewer    a Viewer.
 * @param context   the rendering context.
 */
Viewer::Object Text::insertGeometry(Viewer & viewer,
                                    const VrmlRenderContext context)
{
    const Viewer::Object retval =
            viewer.insertShell(Viewer::MASK_CCW,
                               this->textGeometry.coord.value,
                               this->textGeometry.coordIndex.value,
                               std::vector<color>(), // color
                               std::vector<int32>(), // colorIndex
                               this->textGeometry.normal.value,
                               std::vector<int32>(), // normalIndex
                               std::vector<vec2f>(), // texCoord
                               std::vector<int32>()); // texCoordIndex
    if (this->fontStyle.value) { this->fontStyle.value->clearModified(); }
    return retval;
}

/**
 * @brief Initialize.
 *
 * @param timestamp the current time.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Text::do_initialize(const double timestamp) throw (std::bad_alloc)
{
    this->updateUcs4();
    this->updateFace();
    this->updateGeometry();
}

/**
 * @brief Shut down.
 *
 * @param timestamp the current time.
 */
void Text::do_shutdown(const double timestamp) throw ()
{
# if OPENVRML_ENABLE_TEXT_NODE
    if (this->face) {
        FT_Error ftError = FT_Done_Face(this->face);
        assert(ftError == FT_Err_Ok); // Surely this can't fail.
    }
# endif // OPENVRML_ENABLE_TEXT_NODE
}

/**
 * @brief set_string eventIn handler.
 *
 * @param mfstring  an MFString value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfstring is not an MFString.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Text::processSet_string(const FieldValue & mfstring,
                             const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->string = dynamic_cast<const MFString &>(mfstring);
    this->updateUcs4();
    this->updateGeometry();
    this->setModified();
    this->emitEvent("string_changed", this->string, timestamp);
}

/**
 * @brief set_fontStyle eventIn handler.
 *
 * @param sfnode    an SFNode value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfnode is not an SFNode.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Text::processSet_fontStyle(const FieldValue & sfnode,
                                const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->fontStyle = dynamic_cast<const SFNode &>(sfnode);
    this->updateFace();
    this->updateGeometry();
    this->setModified();
    this->emitEvent("fontStyle_changed", this->fontStyle, timestamp);
}

/**
 * @brief set_length eventIn handler.
 *
 * @param mffloat   an MFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mffloat is not an MFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Text::processSet_length(const FieldValue & mffloat,
                             const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    this->length = dynamic_cast<const MFFloat &>(mffloat);
    this->updateGeometry();
    this->setModified();
    this->emitEvent("length_changed", this->length, timestamp);
}

/**
 * @brief set_maxExtent eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Text::processSet_maxExtent(const FieldValue & sffloat,
                                const double timestamp) throw (std::bad_cast)
{
    this->maxExtent = dynamic_cast<const SFFloat &>(sffloat);
    this->updateGeometry();
    this->setModified();
    this->emitEvent("maxExtent_changed", this->maxExtent, timestamp);
}

/**
 * @brief Called when @a string changes to update the UCS-4 text.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Text::updateUcs4() throw (std::bad_alloc)
{
# ifdef OPENVRML_ENABLE_TEXT_NODE
    this->ucs4String.clear();
    this->ucs4String.resize(this->string.value.size());

    for (size_t i = 0; i < this->string.value.size(); ++i) {
        using std::string;
        using std::vector;

        const string & element = this->string.value[i];

        vector<FcChar32> & ucs4Element = this->ucs4String[i];

        //
        // First, we need to convert the characters from UTF-8 to UCS-4.
        //
        vector<FcChar8> utf8String(element.begin(), element.end());
        int nchar = 0, wchar = 0;
        FcUtf8Len(&utf8String[0], utf8String.size(), &nchar, &wchar);
        ucs4Element.resize(nchar);
        {
            vector<FcChar8>::iterator utf8itr = utf8String.begin();
            vector<FcChar32>::iterator ucs4itr = ucs4Element.begin();
            while (utf8itr != utf8String.end()) {
                const int utf8bytes = FcUtf8ToUcs4(&*utf8itr, &*ucs4itr,
                                                   utf8String.end() - utf8itr);
                utf8itr += utf8bytes;
                ucs4itr++;
            }
        }
    }
# endif // OPENVRML_ENABLE_TEXT_NODE
}

/**
 * @brief Called when @a fontStyle changes to update the font face.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Text::updateFace() throw (std::bad_alloc)
{
# ifdef OPENVRML_ENABLE_TEXT_NODE
    static const char * const fcResultMessage[] = { "match",
                                                    "no match",
                                                    "type mismatch",
                                                    "no id" };

    class FontconfigError : public std::runtime_error {
    public:
        explicit FontconfigError(const FcResult result):
            std::runtime_error(fcResultMessage[result])
        {}

        virtual ~FontconfigError() throw ()
        {}
    };

    class FreeTypeError : public std::runtime_error {
    public:
        //
        // The normal build of FreeType doesn't include a means of mapping
        // error codes to human-readable strings.  There's a means of letting
        // client apps do this by defining some macros, but that's too much
        // trouble for now.
        //
        explicit FreeTypeError(const FT_Error error):
            std::runtime_error("FreeType error.")
        {}

        virtual ~FreeTypeError() throw ()
        {}
    };

    using std::string;
    typedef std::basic_string<FcChar8> FcChar8String;

    FcChar8String language;

    std::vector<string> family;
    family.push_back("SERIF");

    string style;

    FontStyleNode * const fontStyle = this->fontStyle.value
                                    ? this->fontStyle.value->toFontStyle()
                                    : 0;
    if (fontStyle) {
        if (!fontStyle->getFamily().empty()) {
            family = fontStyle->getFamily();
            style = fontStyle->getStyle();
            language.assign(fontStyle->getLanguage().begin(),
                            fontStyle->getLanguage().end());
        }
    }

    try {
        FcPattern * initialPattern = 0;
        FcPattern * matchedPattern = 0;

        try {
            using std::vector;

            string fontName;
            //
            // Set the family.
            //
            for (size_t i = 0; i < family.size(); ++i) {
                const std::string & element = family[i];
                if (element == "SERIF") {
                    fontName += "serif";
                } else if (element == "SANS") {
                    fontName += "sans";
                } else if (element == "TYPEWRITER") {
                    fontName += "monospace";
                } else {
                    fontName += element;
                }
                if (i + 1 < family.size()) { fontName += ", "; }
            }

            //
            // Set the weight.
            //
            if (style.find("BOLD") != string::npos) {
                fontName += ":bold";
            }

            //
            // Set the slant.
            //
            if (style.find("ITALIC") != string::npos) {
                fontName += ":italic";
            }

            //
            // For now, at least, we only want outline fonts.
            //
            fontName += ":outline=True";

            initialPattern = FcNameParse(FcChar8String(fontName.begin(),
                                                       fontName.end()).c_str());
            if (!initialPattern) { throw std::bad_alloc(); }

            //
            // Set the language.
            //
            if (!language.empty()) {
                FcPatternAddString(initialPattern, FC_LANG, language.c_str());
            }

            FcConfigSubstitute(0, initialPattern, FcMatchPattern);
            FcDefaultSubstitute(initialPattern);

            FcResult result = FcResultMatch;
            matchedPattern = FcFontMatch(0, initialPattern, &result);
            if (result != FcResultMatch) { throw FontconfigError(result); }
            assert(matchedPattern);

            FcChar8 * filename = 0;
            result = FcPatternGetString(matchedPattern, FC_FILE, 0, &filename);
            if (result != FcResultMatch) { throw FontconfigError(result); }

            int id = 0;
            result = FcPatternGetInteger(matchedPattern, FC_INDEX, 0, &id);
            if (result != FcResultMatch) { throw FontconfigError(result); }

            TextClass & nodeClass =
                    static_cast<TextClass &>(this->nodeType.nodeClass);

            size_t filenameLen = 0;
            for (; filename[filenameLen]; ++filenameLen);

            const vector<char> ftFilename(filename, filename + filenameLen + 1);

            FT_Face newFace = 0;
            FT_Error ftError = FT_Err_Ok;
            ftError = FT_New_Face(nodeClass.freeTypeLibrary,
                                  &ftFilename[0], id, &newFace);
            if (ftError) { throw FreeTypeError(ftError); }

            if (this->face) {
                ftError = FT_Done_Face(this->face);
                assert(ftError == FT_Err_Ok); // Surely this can't fail.
            }

            this->face = newFace;
            this->glyphGeometryMap.clear();

            FcPatternDestroy(initialPattern);
            FcPatternDestroy(matchedPattern);
        } catch (std::runtime_error & ex) {
            FcPatternDestroy(initialPattern);
            FcPatternDestroy(matchedPattern);
            throw;
        }
    } catch (std::bad_alloc & ex) {
        throw;
    } catch (FontconfigError & ex) {
        OPENVRML_PRINT_EXCEPTION_(ex);
    } catch (FreeTypeError & ex) {
        OPENVRML_PRINT_EXCEPTION_(ex);
    }
# endif // OPENVRML_ENABLE_TEXT_NODE
}

# ifdef OPENVRML_ENABLE_TEXT_NODE
namespace {

    struct GlyphContours_ {
        const float scale;
        std::vector<MFVec2f> contours;

        explicit GlyphContours_(float scale);
    };

    GlyphContours_::GlyphContours_(const float scale):
        scale(scale)
    {}

    const float stepSize_ = 0.2;

    int moveTo_(FT_Vector * const to, void * const user) throw ()
    {
        assert(user);
        GlyphContours_ & c = *static_cast<GlyphContours_ *>(user);
        try {
            c.contours.push_back(MFVec2f(1));
        } catch (std::bad_alloc & ex) {
            OPENVRML_PRINT_EXCEPTION_(ex);
            return FT_Err_Out_Of_Memory;
        }
        const vec2f vertex(to->x * c.scale, to->y * c.scale);
        c.contours.back().value[0] = vertex;
        return 0;
    }

    int lineTo_(FT_Vector * const to, void * const user) throw ()
    {
        assert(user);
        GlyphContours_ & c = *static_cast<GlyphContours_ *>(user);
        const vec2f vertex(to->x * c.scale, to->y * c.scale);
        try {
            c.contours.back().value.push_back(vertex);
        } catch (std::bad_alloc & ex) {
            OPENVRML_PRINT_EXCEPTION_(ex);
            return FT_Err_Out_Of_Memory;
        }
        return 0;
    }

    /**
     * @brief de Casteljau's algorithm.
     *
     * This is a nice recursive algorithm defined by de-Casteljau which
     * calculates for a given control polygon the point that lies on the bezier
     * curve for any value of t, and can be used to evaluate and draw the
     * Bezier spline without using the Bernstein polynomials.
     *
     * The algorithm advances by creating in each step a polygons of degree one
     * less than the one created in the previous step until there is only one
     * point left, which is the point on the curve. The polygon vertices for
     * each step are defined by linear interpolation of two consecutive
     * vertices of the polygon from the previous step with a value of t (the
     * parameter):
     *
     * @param buffer    an array including the control points for the curve in
     *                  the first @p npoints elements. The total size of the
     *                  array must be @p npoints * @p npoints. The remaining
     *                  elements of the array will be used by the algorithm to
     *                  store temporary values.
     * @param npoints   the number of control points.
     * @param contour   the points on the curve are added to this array.
     *
     * @exception std::bad_alloc    if memory allocation fails.
     */
    void evaluateCurve_(vec2f * buffer, const size_t npoints, MFVec2f & contour)
        throw (std::bad_alloc)
    {
        for (size_t i = 1; i <= (1 / stepSize_); i++){
            const float t = i * stepSize_; // Parametric points 0 <= t <= 1
            for (size_t j = 1; j < npoints; j++) {
                for (size_t k = 0; k < (npoints - j); k++) {
                    buffer[j * npoints + k][0] =
                            (1 - t) * buffer[(j - 1) * npoints + k][0]
                            + t * buffer[(j - 1) * npoints + k + 1][0];
                    buffer[j * npoints + k][1] =
                            (1 - t) * buffer[(j - 1) * npoints + k][1]
                            + t * buffer[(j - 1) * npoints + k + 1][1];
                }
            }
            //
            // Specify next vertex to be included on curve
            //
            contour.value.push_back(buffer[(npoints - 1) * npoints]); // throws std::bad_alloc
        }
    }

    int conicTo_(FT_Vector * const control, FT_Vector * const to,
                 void * const user)
        throw ()
    {
        assert(control);
        assert(to);
        assert(user);
        GlyphContours_ & c = *static_cast<GlyphContours_ *>(user);

        assert(!c.contours.empty());
        MFVec2f & contour = c.contours.back();
        const vec2f & lastVertex = contour.value[contour.value.size() - 1];

        assert(!contour.value.empty());
        const size_t npoints = 3;
        vec2f buffer[npoints * npoints] = {
            vec2f(lastVertex[0], lastVertex[1]),
            vec2f(control->x * c.scale, control->y * c.scale),
            vec2f(to->x * c.scale, to->y * c.scale)
        };

        try {
            evaluateCurve_(buffer, npoints, contour);
        } catch (std::bad_alloc & ex) {
            OPENVRML_PRINT_EXCEPTION_(ex);
            return FT_Err_Out_Of_Memory;
        }
        return 0;
    }

    int cubicTo_(FT_Vector * const control1, FT_Vector * const control2,
                 FT_Vector * const to, void * const user)
        throw ()
    {
        assert(control1);
        assert(control2);
        assert(to);
        assert(user);
        GlyphContours_ & c = *static_cast<GlyphContours_ *>(user);

        assert(!c.contours.empty());
        MFVec2f & contour = c.contours.back();
        const vec2f & lastVertex = contour.value[contour.value.size() - 1];

        assert(!contour.value.empty());
        const size_t npoints = 4;
        vec2f buffer[npoints * npoints] = {
            vec2f(lastVertex[0], lastVertex[1]),
            vec2f(control1->x * c.scale, control1->y * c.scale),
            vec2f(control2->x * c.scale, control2->y * c.scale),
            vec2f(to->x * c.scale, to->y * c.scale)
        };

        try {
            evaluateCurve_(buffer, npoints, contour);
        } catch (std::bad_alloc & ex) {
            OPENVRML_PRINT_EXCEPTION_(ex);
            return FT_Err_Out_Of_Memory;
        }
        return 0;
    }
}
# endif // OPENVRML_ENABLE_TEXT_NODE

/**
 * @brief Called to update @a textGeometry.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Text::updateGeometry() throw (std::bad_alloc)
{
# ifdef OPENVRML_ENABLE_TEXT_NODE
    using std::pair;
    using std::string;
    using std::vector;

    bool horizontal = true;
    string justify[2] = { "BEGIN", "FIRST" };
    bool leftToRight = true;
    bool topToBottom = true;
    float size = 1.0;
    float spacing = 1.0;
    FontStyleNode * fontStyle;
    if (this->fontStyle.value
            && (fontStyle = this->fontStyle.value->toFontStyle())) {
        horizontal = fontStyle->getHorizontal();
        if (!fontStyle->getJustify().empty()) {
            justify[0] = fontStyle->getJustify()[0];
        }
        if (fontStyle->getJustify().size() > 1) {
            justify[1] = fontStyle->getJustify()[1];
        }
        leftToRight = fontStyle->getLeftToRight();
        topToBottom = fontStyle->getTopToBottom();
        size = fontStyle->getSize();
        spacing = fontStyle->getSpacing();
    }

    TextGeometry newGeometry;
    float geometryXMin = 0.0, geometryXMax = 0.0;
    float geometryYMin = 0.0, geometryYMax = 0.0;
    size_t npolygons = 0;
    const Ucs4String::const_iterator stringBegin = this->ucs4String.begin();
    for (Ucs4String::const_iterator string = stringBegin;
            string != this->ucs4String.end(); ++string) {
        float penPos[2] = { 0.0, 0.0 };
        const size_t line = std::distance(stringBegin, string);
        const float lineAdvance = size * spacing * line;
        if (horizontal) {
            if (topToBottom) {
                penPos[1] -= lineAdvance;
            } else {
                penPos[1] += lineAdvance;
            }
        } else {
            if (leftToRight) {
                penPos[0] += lineAdvance;
            } else {
                penPos[0] -= lineAdvance;
            }
        }

        struct LineGeometry {
            MFVec2f coord;
            MFInt32 coordIndex;
            float xMin, xMax;
            float yMin, yMax;
        };

        LineGeometry lineGeometry = {};
        for (vector<FcChar32>::const_iterator character = string->begin();
                character != string->end(); ++character) {
            assert(this->face);
            const FT_UInt glyphIndex =
                    FcFreeTypeCharIndex(this->face, *character);

            const GlyphGeometry * glyphGeometry = 0;
            const GlyphGeometryMap::iterator pos =
                    this->glyphGeometryMap.find(glyphIndex);
            if (pos != this->glyphGeometryMap.end()) {
                glyphGeometry = &pos->second;
            } else {
                FT_Error error = FT_Err_Ok;
                error = FT_Load_Glyph(this->face, glyphIndex, FT_LOAD_NO_SCALE);
                assert(error == FT_Err_Ok);
                FT_Glyph glyph;
                error = FT_Get_Glyph(this->face->glyph, &glyph);
                assert(error == FT_Err_Ok);
                static FT_Outline_Funcs outlineFuncs = { moveTo_,
                                                         lineTo_,
                                                         conicTo_,
                                                         cubicTo_,
                                                         0,
                                                         0 };
                const float glyphScale = (this->face->bbox.yMax > 0.0)
                                       ? size / this->face->bbox.yMax
                                       : size;
                GlyphContours_ glyphContours(glyphScale);
                assert(glyph->format == ft_glyph_format_outline);
                const FT_OutlineGlyph outlineGlyph =
                        reinterpret_cast<FT_OutlineGlyph>(glyph);
                error = FT_Outline_Decompose(&outlineGlyph->outline,
                                             &outlineFuncs,
                                             &glyphContours);
                assert(error == FT_Err_Ok);

                assert(this->face->glyph);
                const float advanceWidth =
                        FT_HAS_HORIZONTAL(this->face)
                        ? this->face->glyph->metrics.horiAdvance * glyphScale
                        : 0.0;
                const float advanceHeight =
                        FT_HAS_VERTICAL(this->face)
                        ? this->face->glyph->metrics.vertAdvance * glyphScale
                        : 0.0;

                const GlyphGeometryMap::value_type
                        value(glyphIndex, GlyphGeometry(glyphContours.contours,
                                                        advanceWidth,
                                                        advanceHeight));
                const pair<GlyphGeometryMap::iterator, bool> result =
                        this->glyphGeometryMap.insert(value);
                assert(result.second);
                glyphGeometry = &result.first->second;
            }

            for (size_t i = 0; i < glyphGeometry->coord.value.size(); ++i) {
                const vec2f & glyphVertex = glyphGeometry->coord.value[i];
                const vec2f textVertex(glyphVertex[0] + penPos[0],
                                       glyphVertex[1] + penPos[1]);
                lineGeometry.coord.value.push_back(textVertex);
                lineGeometry.xMin = (lineGeometry.xMin < textVertex[0])
                                  ? lineGeometry.xMin
                                  : textVertex[0];
                lineGeometry.xMax = (lineGeometry.xMax > textVertex[0])
                                  ? lineGeometry.xMax
                                  : textVertex[0];
                lineGeometry.yMin = (lineGeometry.yMin < textVertex[1])
                                  ? lineGeometry.yMin
                                  : textVertex[1];
                lineGeometry.yMax = (lineGeometry.yMax > textVertex[1])
                                  ? lineGeometry.yMax
                                  : textVertex[1];
            }

            for (size_t i = 0; i < glyphGeometry->coordIndex.value.size(); ++i) {
                const long index = glyphGeometry->coordIndex.value[i];
                if (index > -1) {
                    const size_t offset = lineGeometry.coord.value.size()
                                          - glyphGeometry->coord.value.size();
                    lineGeometry.coordIndex.value.push_back(offset + index);
                } else {
                    lineGeometry.coordIndex.value.push_back(-1);
                    ++npolygons;
                }
            }
            if (horizontal) {
                const float xAdvance = glyphGeometry->advanceWidth;
                if (leftToRight) {
                    penPos[0] += xAdvance;
                } else {
                    penPos[0] -= xAdvance;
                }
            } else {
                const float yAdvance = glyphGeometry->advanceHeight;
                if (topToBottom) {
                    penPos[1] -= yAdvance;
                } else {
                    penPos[1] += yAdvance;
                }
            }
        }

        //
        // Scale to length.
        //
        const float length = (line < this->length.value.size())
                           ? this->length.value[line]
                           : 0.0;
        if (length > 0.0) {
            const float currentLength = lineGeometry.xMax - lineGeometry.xMin;
            for (size_t i = 0; i < lineGeometry.coord.value.size(); ++i) {
                const vec2f & vertex = lineGeometry.coord.value[i];
                const vec2f scaledVertex(vertex[0] / currentLength * length,
                                         vertex[1]);
                lineGeometry.coord.value[i] = scaledVertex;
            }
        }

        //
        // Add the line to the text geometry. We need to adjust for the major
        // alignment.
        //
        float xOffset = 0.0f, yOffset = 0.0f;
        //
        // Offset is 0 for "BEGIN" or "FIRST" (or anything else, in our case).
        //
        if (justify[0] == "MIDDLE") {
            if (horizontal) {
                xOffset = -((lineGeometry.xMax - lineGeometry.xMin) / 2.0f);
            } else {
                yOffset = (lineGeometry.yMax - lineGeometry.yMin) / 2.0f;
            }
        } else if (justify[0] == "END") {
            if (horizontal) {
                xOffset = -(lineGeometry.xMax - lineGeometry.xMin);
            } else {
                yOffset = lineGeometry.yMax - lineGeometry.yMin;
            }
        }
        for (size_t i = 0; i < lineGeometry.coordIndex.value.size(); ++i) {
            const long index = lineGeometry.coordIndex.value[i];
            if (index > -1) {
                const vec2f & lineVertex = lineGeometry.coord.value[index];
                const vec3f textVertex(lineVertex.x() + xOffset,
                                       lineVertex.y() + yOffset,
                                       0.0f);
                newGeometry.coord.value.push_back(textVertex);
                newGeometry.coordIndex
                        .value.push_back(newGeometry.coord.value.size() - 1);
                geometryXMin = (geometryXMin < textVertex.x())
                             ? geometryXMin
                             : textVertex.x();
                geometryXMax = (geometryXMax > textVertex.x())
                             ? geometryXMax
                             : textVertex.x();
                geometryYMin = (geometryYMin < textVertex.y())
                             ? geometryYMin
                             : textVertex.y();
                geometryYMax = (geometryYMax > textVertex.y())
                             ? geometryYMax
                             : textVertex.y();
            } else {
                newGeometry.coordIndex.value.push_back(-1);
            }
        }
    }

    //
    // Scale to maxExtent.
    //
    const float maxExtent = (this->maxExtent.value > 0.0)
                          ? this->maxExtent.value
                          : 0.0;
    if (maxExtent > 0.0) {
        const float currentMaxExtent = geometryXMax - geometryXMin;
        if (currentMaxExtent > maxExtent) {
            for (size_t i = 0; i < newGeometry.coord.value.size(); ++i) {
                const vec3f & vertex = newGeometry.coord.value[i];
                const vec3f scaledVertex(
                    vertex.x() / currentMaxExtent * maxExtent,
                    vertex.y(),
                    vertex.z()
                );
                newGeometry.coord.value[i] = scaledVertex;
            }
        }
    }

    //
    // Adjust for the minor alignment.
    //
    float xOffset = 0.0f, yOffset = 0.0f;
    if (justify[1] == "FIRST" || justify[1] == "") {
    } else if (justify[1] == "BEGIN") {
        if (horizontal) {
            yOffset = -(size * spacing);
        } else {
            xOffset = 0.0f;
        }
    } else if (justify[1] == "MIDDLE") {
        if (horizontal) {
            yOffset = ((size * spacing * this->string.value.size()) / 2.0f)
                      - (size * spacing);
        } else {
            xOffset = ((size * spacing * this->string.value.size()) / 2.0f)
                      - (size * spacing);
        }
    } else if (justify[1] == "END") {
        if (horizontal) {
            yOffset = size * spacing * (this->string.value.size() - 1);
        } else {
            xOffset = size * spacing * (this->string.value.size() - 1);
        }
    }
    for (size_t i = 0; i < newGeometry.coord.value.size(); ++i) {
        const vec3f & vertex = newGeometry.coord.value[i];
        const vec3f adjustedVertex(vertex.x() + xOffset,
                                   vertex.y() + yOffset,
                                   vertex.z());
        newGeometry.coord.value[i] = adjustedVertex;
    }

    //
    // Create the normals.
    //
    newGeometry.normal.value.resize(npolygons);
    for (size_t i = 0; i < newGeometry.normal.value.size(); ++i) {
        static const vec3f normal(0.0, 0.0, 1.0);
        newGeometry.normal.value[i] = normal;
    }

    this->textGeometry = newGeometry;
# endif // OPENVRML_ENABLE_TEXT_NODE
}


/**
 * @class TextureCoordinateClass
 *
 * @brief Class object for TextureCoordinate nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
TextureCoordinateClass::TextureCoordinateClass(Browser & browser):
        NodeClass(browser) {}

/**
 * @brief Destructor.
 */
TextureCoordinateClass::~TextureCoordinateClass() throw () {}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating TextureCoordinate
 *      nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by TextureCoordinateClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
        TextureCoordinateClass::createType(const std::string & id,
                                           const NodeInterfaceSet & interfaces)
        throw (UnsupportedInterface, std::bad_alloc) {
    static const NodeInterface supportedInterface =
            NodeInterface(NodeInterface::exposedField, FieldValue::mfvec2f, "point");
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<TextureCoordinate>(*this, id));
    Vrml97NodeTypeImpl<TextureCoordinate> & textureCoordinateNodeType =
            static_cast<Vrml97NodeTypeImpl<TextureCoordinate> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<TextureCoordinate>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterface) {
            textureCoordinateNodeType.addExposedField(
                supportedInterface.fieldType,
                supportedInterface.id,
                &TextureCoordinate::processSet_point,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TextureCoordinate, MFVec2f>
                                    (&TextureCoordinate::point)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class TextureCoordinate
 *
 * @brief TextureCoordinate node instances.
 */

/**
 * @var TextureCoordinate::TextureCoordinateClass
 *
 * @brief Class object for TextureCoordinate instances.
 */

/**
 * @var MFVec2f TextureCoordinate::point
 *
 * @brief point exposedField.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType      the NodeType associated with the instance.
 * @param scope         the Scope that the new node will belong to.
 */
TextureCoordinate::TextureCoordinate(const NodeType & nodeType,
                                     const ScopePtr & scope):
        Node(nodeType, scope),
        AbstractBase(nodeType, scope),
        TextureCoordinateNode(nodeType, scope) {}

/**
 * @brief Destructor.
 */
TextureCoordinate::~TextureCoordinate() throw () {}

/**
 * @brief Get the points encapsulated by this node.
 *
 * @return the MFVec2f array of points for this node.
 */
const std::vector<vec2f> & TextureCoordinate::getPoint() const throw ()
{
    return this->point.value;
}

/**
 * @brief set_point eventIn handler.
 *
 * @param mfvec2f   an array of vectors representing points.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p mfvec2f is not an MFVec2f.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void TextureCoordinate::processSet_point(const FieldValue & mfvec2f,
                                         const double timestamp)
        throw (std::bad_cast, std::bad_alloc) {
    this->point = dynamic_cast<const MFVec2f &>(mfvec2f);
    this->setModified();
    this->emitEvent("point_changed", this->point, timestamp);
}


/**
 * @class TextureTransformClass
 *
 * @brief Class object for TextureTransform nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
TextureTransformClass::TextureTransformClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
TextureTransformClass::~TextureTransformClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating TextureTransform
 *      nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                                  supported by TextureTransformClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
TextureTransformClass::createType(const std::string & id,
                                  const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec2f, "center"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "rotation"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec2f, "scale"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec2f, "translation")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<TextureTransform>(*this, id));
    Vrml97NodeTypeImpl<TextureTransform> & textureTransformNodeType =
            static_cast<Vrml97NodeTypeImpl<TextureTransform> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<TextureTransform>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            textureTransformNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &TextureTransform::processSet_center,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TextureTransform, SFVec2f>
                                    (&TextureTransform::center)));
        } else if (*itr == supportedInterfaces[1]) {
            textureTransformNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &TextureTransform::processSet_rotation,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TextureTransform, SFFloat>
                                    (&TextureTransform::rotation)));
        } else if (*itr == supportedInterfaces[2]) {
            textureTransformNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &TextureTransform::processSet_scale,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TextureTransform, SFVec2f>
                                    (&TextureTransform::scale)));
        } else if (*itr == supportedInterfaces[3]) {
            textureTransformNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &TextureTransform::processSet_translation,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TextureTransform, SFVec2f>
                                    (&TextureTransform::translation)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class TextureTransform
 *
 * @brief TextureTransform node instances.
 */

/**
 * @var TextureTransform::TextureTransformClass
 *
 * @brief Class object for TextureTransform instances.
 */

/**
 * @var SFVec2f TextureTransform::center
 *
 * @brief center exposedField.
 */

/**
 * @var SFFloat TextureTransform::rotation
 *
 * @brief rotation exposedField.
 */

/**
 * @var SFVec2f TextureTransform::scale
 *
 * @brief scale exposedField.
 */

/**
 * @var SFVec2f TextureTransform::translation
 *
 * @brief translation exposedField.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType      the NodeType associated with the instance.
 * @param scope         the Scope that the new node will belong to.
 */
TextureTransform::TextureTransform(const NodeType & nodeType,
                                   const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    TextureTransformNode(nodeType, scope),
    center(vec2f(0.0, 0.0)),
    rotation(0.0),
    scale(vec2f(1.0, 1.0)),
    translation(vec2f(0.0, 0.0))
{}

/**
 * @brief Destructor.
 */
TextureTransform::~TextureTransform() throw ()
{}

/**
 * @brief Render the node.
 *
 * @param viewer    a Viewer.
 * @param context   a rendering context.
 */
void TextureTransform::render(Viewer & viewer, const VrmlRenderContext context)
{
    viewer.setTextureTransform(this->center.value,
                               this->rotation.value,
                               this->scale.value,
                               this->translation.value);
    this->clearModified();
}

/**
 * @brief set_center eventIn handler.
 *
 * @param sfvec2f   an SFVec2f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec2f is not an SFVec2f.
 */
void TextureTransform::processSet_center(const FieldValue & sfvec2f,
                                         const double timestamp)
    throw (std::bad_cast)
{
    this->center = dynamic_cast<const SFVec2f &>(sfvec2f);
    this->setModified();
    this->emitEvent("center_changed", this->center, timestamp);
}

/**
 * @brief set_rotation eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sffloat is not an SFFloat.
 */
void TextureTransform::processSet_rotation(const FieldValue & sffloat,
                                           const double timestamp)
    throw (std::bad_cast)
{
    this->rotation = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("rotation_changed", this->rotation, timestamp);
}

/**
 * @brief set_scale eventIn handler.
 *
 * @param sfvec2f   an SFVec2f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec2f is not an SFVec2f.
 */
void TextureTransform::processSet_scale(const FieldValue & sfvec2f,
                                        const double timestamp)
    throw (std::bad_cast)
{
    this->scale = dynamic_cast<const SFVec2f &>(sfvec2f);
    this->setModified();
    this->emitEvent("scale_changed", this->scale, timestamp);
}

/**
 * @brief set_translation eventIn handler.
 *
 * @param sfvec2f   an SFVec2f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec2f is not an SFVec2f.
 */
void TextureTransform::processSet_translation(const FieldValue & sfvec2f,
                                              const double timestamp)
    throw (std::bad_cast)
{
    this->translation = dynamic_cast<const SFVec2f &>(sfvec2f);
    this->setModified();
    this->emitEvent("translation_changed", this->translation, timestamp);
}


/**
 * @class TimeSensorClass
 *
 * @brief Class object for TimeSensor nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser   the Browser associated with this NodeClass.
 */
TimeSensorClass::TimeSensorClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
TimeSensorClass::~TimeSensorClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating TimeSensor nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                                  supported by TimeSensorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
TimeSensorClass::createType(const std::string & id,
                            const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sftime, "cycleInterval"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "enabled"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "loop"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sftime, "startTime"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sftime, "stopTime"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sftime, "cycleTime"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sffloat, "fraction_changed"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isActive"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sftime, "time")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<TimeSensor>(*this, id));
    Vrml97NodeTypeImpl<TimeSensor> & timeSensorNodeType =
            static_cast<Vrml97NodeTypeImpl<TimeSensor> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<TimeSensor>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            timeSensorNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &TimeSensor::processSet_cycleInterval,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TimeSensor, SFTime>
                                    (&TimeSensor::cycleInterval)));
        } else if (*itr == supportedInterfaces[1]) {
            timeSensorNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &TimeSensor::processSet_enabled,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TimeSensor, SFBool>
                                    (&TimeSensor::enabled)));
        } else if (*itr == supportedInterfaces[2]) {
            timeSensorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &TimeSensor::processSet_loop,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TimeSensor, SFBool>
                                    (&TimeSensor::loop)));
        } else if (*itr == supportedInterfaces[3]) {
            timeSensorNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &TimeSensor::processSet_startTime,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TimeSensor, SFTime>
                                    (&TimeSensor::startTime)));
        } else if (*itr == supportedInterfaces[4]) {
            timeSensorNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &TimeSensor::processSet_stopTime,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TimeSensor, SFTime>
                                    (&TimeSensor::stopTime)));
        } else if (*itr == supportedInterfaces[5]) {
            timeSensorNodeType.addEventOut(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TimeSensor, SFTime>
                                    (&TimeSensor::cycleTime)));
        } else if (*itr == supportedInterfaces[6]) {
            timeSensorNodeType.addEventOut(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TimeSensor, SFFloat>
                                    (&TimeSensor::fraction)));
        } else if (*itr == supportedInterfaces[7]) {
            timeSensorNodeType.addEventOut(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TimeSensor, SFBool>
                                    (&TimeSensor::active)));
        } else if (*itr == supportedInterfaces[8]) {
            timeSensorNodeType.addEventOut(
                supportedInterfaces[8].fieldType,
                supportedInterfaces[8].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TimeSensor, SFTime>
                                    (&TimeSensor::time)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class TimeSensor
 *
 * @brief TimeSensor node instances.
 */

/**
 * @var TimeSensor::TimeSensorClass
 *
 * @brief Class object for TimeSensor instances.
 */

/**
 * @var SFTime TimeSensor::cycleInterval
 *
 * @brief cycleInterval exposedField.
 */

/**
 * @var SFBool TimeSensor::enabled
 *
 * @brief enabled exposedField.
 */

/**
 * @var SFBool TimeSensor::loop
 *
 * @brief loop exposedField.
 */

/**
 * @var SFTime TimeSensor::startTime
 *
 * @brief startTime exposedField.
 */

/**
 * @var SFTime TimeSensor::stopTime
 *
 * @brief stopTime exposedField.
 */

/**
 * @var SFTime TimeSensor::cycleTime
 *
 * @brief cycleTime eventOut.
 */

/**
 * @var SFFloat TimeSensor::fraction
 *
 * @brief fraction_changed eventOut.
 */

/**
 * @var SFBool TimeSensor::active
 *
 * @brief isActive eventOut.
 */

/**
 * @var SFTime TimeSensor::time
 *
 * @brief time eventOut.
 */

/**
 * @var double TimeSensor::lastTime
 *
 * @brief The timestamp previously received.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType      the NodeType associated with the instance.
 * @param scope         the Scope that the new node will belong to.
 */
TimeSensor::TimeSensor(const NodeType & nodeType,
                       const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope),
    cycleInterval(1.0),
    enabled(true),
    loop(false),
    startTime(0.0),
    stopTime(0.0),
    active(false),
    lastTime(-1.0)
{}

/**
 * @brief Destructor.
 */
TimeSensor::~TimeSensor() throw ()
{}

/**
 * @brief Cast to a TimeSensor.
 *
 * @return a pointer to the object.
 */
TimeSensor * TimeSensor::toTimeSensor() const
{
    return (TimeSensor*) this;
}

/**
 * Generate timer events. If necessary, events prior to the timestamp (inTime)
 * are generated to respect stopTimes and cycleIntervals. The timestamp
 * should never be increased. This assumes the event loop delivers pending
 * events in order (ascending time stamps). Should inTime be modified?
 * Should ensure continuous events are delivered before discrete ones
 * (such as cycleTime, isActive).
 */
void TimeSensor::update(const double currentTime)
{
    using OpenVRML_::fpzero;
    using OpenVRML_::fpequal;

    SFTime timeNow(currentTime);

    if (this->enabled.value) {
        if (this->lastTime > timeNow.value) { this->lastTime = timeNow.value; }

        // Become active at startTime if either the valid stopTime hasn't
        // passed or we are looping.
        if (!this->active.value
                && this->startTime.value <= timeNow.value
                && this->startTime.value >= this->lastTime
                && ((this->stopTime.value < this->startTime.value
                    || this->stopTime.value > timeNow.value)
                    || this->loop.value)) {
            this->active.value = true;

            // Start at first tick >= startTime
            this->emitEvent("isActive", this->active, timeNow.value);
            this->emitEvent("time", timeNow, timeNow.value);
            this->emitEvent("fraction_changed", SFFloat(0.0), timeNow.value);
            this->emitEvent("cycleTime", timeNow, timeNow.value);
        }

        // Running (active and enabled)
        else if (this->active.value) {
            double f, cycleInt = this->cycleInterval.value;
            bool deactivate = false;

            // Are we done? Choose min of stopTime or start + single cycle.
            if ((this->stopTime.value > this->startTime.value
                        && this->stopTime.value <= timeNow.value)
                    || (!this->loop.value
                        && this->startTime.value + cycleInt <= timeNow.value)) {
                this->active.value = false;

                // Must respect stopTime/cycleInterval exactly
                if (this->startTime.value + cycleInt < this->stopTime.value) {
                    timeNow = SFTime(this->startTime.value + cycleInt);
                } else {
                    timeNow = this->stopTime;
                }

                deactivate = true;
            }

            if (cycleInt > 0.0 && timeNow.value > this->startTime.value) {
                f = fmod(timeNow.value - this->startTime.value, cycleInt);
            } else {
                f = 0.0;
            }

            // Fraction of cycle message
            SFFloat fraction_changed(fpzero(f) ? 1.0 : (f / cycleInt));
            this->emitEvent("fraction_changed", fraction_changed, timeNow.value);

            // Current time message
            this->emitEvent("time", timeNow, timeNow.value);

            // End of cycle message (this may miss cycles...)
            if (fpequal(fraction_changed.value, 1.0)) {
                this->emitEvent("cycleTime", timeNow, timeNow.value);
            }

            if (deactivate) {
                this->emitEvent("isActive", this->active, timeNow.value);
            }
        }

        // Tell the scene this node needs quick updates while it is active.
        // Should check whether time, fraction_changed eventOuts are
        // being used, and set delta to cycleTime if not...
        if (this->active.value) {
#ifdef macintosh
            this->nodeType.nodeClass.browser.setDelta(0.001); //0.0 is too fast(!)
#else
            this->nodeType.nodeClass.browser.setDelta(0.0);
#endif
        }
        this->lastTime = currentTime;
    }
}

/**
 * @brief Get the bounding volume.
 *
 * @return the bounding volume associated with the node.
 */
const BVolume * TimeSensor::getBVolume() const
{
    static BSphere * inf_bsphere = 0;
    if (!inf_bsphere) { inf_bsphere = new BSphere(); }
    return inf_bsphere;
}

/**
 * @brief Initialize.
 *
 * @param timestamp the current time.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void TimeSensor::do_initialize(const double timestamp) throw (std::bad_alloc)
{
    assert(this->getScene());
    this->getScene()->browser.addTimeSensor(*this);
}

/**
 * @brief Shut down.
 *
 * @param timestamp the current time.
 */
void TimeSensor::do_shutdown(const double timestamp) throw ()
{
    assert(this->getScene());
    this->getScene()->browser.removeTimeSensor(*this);
}

/**
 * @brief set_cycleInterval eventIn handler.
 *
 * @param sftime    an SFTime value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sftime is not an SFTime.
 */
void TimeSensor::processSet_cycleInterval(const FieldValue & sftime,
                                          const double timestamp)
    throw (std::bad_cast)
{
    if (!this->active.value) {
        this->cycleInterval = dynamic_cast<const SFTime &>(sftime);
        this->lastTime = timestamp;
        this->emitEvent("cycleInterval_changed", this->cycleInterval,
                        timestamp);
    }
}

/**
 * @brief set_enabled eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void TimeSensor::processSet_enabled(const FieldValue & sfbool,
                                    const double timestamp)
    throw (std::bad_cast)
{
    using OpenVRML_::fpzero;

    this->enabled = dynamic_cast<const SFBool &>(sfbool);
    if (this->enabled.value != this->active.value) {
        if (this->active.value) {
            //
            // Was active; shutdown.
            //
            double cycleInt = this->cycleInterval.value;
            double f = (cycleInt > 0.0)
                     ? fmod(this->time.value - this->startTime.value, cycleInt)
                     : 0.0;

            // Fraction of cycle message
            this->fraction.value = fpzero(f) ? 1.0 : (f / cycleInt);
        } else {
            //
            // Was inactive; startup.
            //
            this->cycleTime.value = timestamp;
            this->emitEvent("cycleTime", this->cycleTime, timestamp);

            // Fraction of cycle message
            this->fraction.value = 0.0;
        }
        this->time.value = timestamp;
        this->emitEvent("time", this->time, timestamp);
        this->emitEvent("fraction_changed", this->fraction, timestamp);
        this->active = this->enabled;
        this->emitEvent("isActive", this->active, timestamp);
    }
    this->emitEvent("enabled_changed", this->enabled, timestamp);
}

/**
 * @brief set_loop eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void TimeSensor::processSet_loop(const FieldValue & sfbool,
                                 const double timestamp)
    throw (std::bad_cast)
{
    this->loop = dynamic_cast<const SFBool &>(sfbool);
    this->emitEvent("loop_changed", this->loop, timestamp);
}

/**
 * @brief set_startTime eventIn handler.
 *
 * @param sftime    an SFTime value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sftime is not an SFTime.
 */
void TimeSensor::processSet_startTime(const FieldValue & sftime,
                                      const double timestamp)
    throw (std::bad_cast)
{
    if (!this->active.value) {
        this->startTime = dynamic_cast<const SFTime &>(sftime);
        this->lastTime = timestamp;
        this->emitEvent("startTime_changed", this->startTime, timestamp);
    }
}

/**
 * @brief set_stopTime eventIn handler.
 *
 * @param sftime    an SFTime value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sftime is not an SFTime.
 */
void TimeSensor::processSet_stopTime(const FieldValue & sftime,
                                     const double timestamp)
    throw (std::bad_cast)
{
    this->stopTime = dynamic_cast<const SFTime &>(sftime);
    this->emitEvent("stopTime_changed", this->stopTime, timestamp);
}


/**
 * @class TouchSensorClass
 *
 * @brief Class object for TouchSensor nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
TouchSensorClass::TouchSensorClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
TouchSensorClass::~TouchSensorClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating TouchSensor nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                                  supported by TouchSensorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
TouchSensorClass::createType(const std::string & id,
                             const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "enabled"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfvec3f, "hitNormal_changed"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfvec3f, "hitPoint_changed"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfvec2f, "hitTexCoord_changed"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isActive"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isOver"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sftime, "touchTime")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<TouchSensor>(*this, id));
    Vrml97NodeTypeImpl<TouchSensor> & touchSensorNodeType =
            static_cast<Vrml97NodeTypeImpl<TouchSensor> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<TouchSensor>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            touchSensorNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &TouchSensor::processSet_enabled,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TouchSensor, SFBool>
                                    (&TouchSensor::enabled)));
        } else if (*itr == supportedInterfaces[1]) {
            touchSensorNodeType.addEventOut(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TouchSensor, SFVec3f>
                                    (&TouchSensor::hitNormal)));
        } else if (*itr == supportedInterfaces[2]) {
            touchSensorNodeType.addEventOut(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TouchSensor, SFVec3f>
                                    (&TouchSensor::hitPoint)));
        } else if (*itr == supportedInterfaces[3]) {
            touchSensorNodeType.addEventOut(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TouchSensor, SFVec2f>
                                    (&TouchSensor::hitTexCoord)));
        } else if (*itr == supportedInterfaces[4]) {
            touchSensorNodeType.addEventOut(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TouchSensor, SFBool>
                                    (&TouchSensor::active)));
        } else if (*itr == supportedInterfaces[5]) {
            touchSensorNodeType.addEventOut(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TouchSensor, SFBool>
                                    (&TouchSensor::over)));
        } else if (*itr == supportedInterfaces[6]) {
            touchSensorNodeType.addEventOut(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<TouchSensor, SFTime>
                                    (&TouchSensor::touchTime)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class TouchSensor
 *
 * @brief TouchSensor node instances.
 */

/**
 * @var TouchSensor::TouchSensorClass
 *
 * @brief Class object for TouchSensor instances.
 */

/**
 * @var SFBool TouchSensor::enabled
 *
 * @brief enabled exposedField.
 */

/**
 * @var SFVec3f TouchSensor::hitNormal
 *
 * @brief hitNormal_changed eventOut.
 */

/**
 * @var SFVec3f TouchSensor::hitPoint
 *
 * @brief hitPoint_changed eventOut.
 */

/**
 * @var SFVec2f TouchSensor::hitTexCoord
 *
 * @brief hitTexCoord_changed eventOut.
 */

/**
 * @var SFBool TouchSensor::active
 *
 * @brief isActive eventOut.
 */

/**
 * @var SFBool TouchSensor::over
 *
 * @brief isOver eventOut.
 */

/**
 * @var SFTime TouchSensor::touchTime
 *
 * @brief touchTime eventOut.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType      the NodeType associated with the instance.
 * @param scope         the Scope that the new node will belong to.
 */
TouchSensor::TouchSensor(const NodeType & nodeType,
                         const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope),
    enabled(true),
    active(false),
    over(false),
    touchTime(0.0)
{
    this->setModified();
}

/**
 * @brief Destructor.
 */
TouchSensor::~TouchSensor() throw ()
{}

/**
 * @brief Cast to a TouchSensor.
 *
 * @return a pointer to the object.
 */
TouchSensor* TouchSensor::toTouchSensor() const
{
    return (TouchSensor*) this;
}

/**
 * @todo Doesn't compute the xxx_changed eventOuts yet...
 */
void TouchSensor::activate(double timeStamp, bool isOver, bool isActive,
                           double *)
{
    if (isOver && !isActive && this->active.value) {
        this->touchTime.value = timeStamp;
        this->emitEvent("touchTime", this->touchTime, timeStamp);
    }

    if (isOver != this->over.value) {
        this->over.value = isOver;
        this->emitEvent("isOver", this->over, timeStamp);
    }

    if (isActive != this->active.value) {
        this->active.value = isActive;
        this->emitEvent("isActive", this->active, timeStamp);
    }
    // if (isOver && any routes from eventOuts)
    //   generate xxx_changed eventOuts...
}

/**
 * @brief Return whether the TouchSensor is enabled.
 *
 * @return @c true if the TouchSensor is enabled, @c false otherwise.
 */
bool TouchSensor::isEnabled() const
{
    return this->enabled.value;
}

/**
 * @brief set_enabled eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void TouchSensor::processSet_enabled(const FieldValue & sfbool,
                                     const double timestamp)
    throw (std::bad_cast)
{
    this->enabled = dynamic_cast<const SFBool &>(sfbool);
    this->emitEvent("enabled_changed", this->enabled, timestamp);
}


/**
 * @class TransformClass
 *
 * @brief Class object for Transform nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
TransformClass::TransformClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
TransformClass::~TransformClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Transform nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                              supported by TransformClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
TransformClass::createType(const std::string & id,
                           const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::mfnode, "addChildren"),
        NodeInterface(NodeInterface::eventIn, FieldValue::mfnode, "removeChildren"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "center"),
        NodeInterface(NodeInterface::exposedField, FieldValue::mfnode, "children"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfrotation, "rotation"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "scale"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfrotation, "scaleOrientation"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "translation"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "bboxCenter"),
        NodeInterface(NodeInterface::field, FieldValue::sfvec3f, "bboxSize")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Transform>(*this, id));
    Vrml97NodeTypeImpl<Transform> & transformNodeType =
            static_cast<Vrml97NodeTypeImpl<Transform> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Transform>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            transformNodeType.addEventIn(supportedInterfaces[0].fieldType,
                                      supportedInterfaces[0].id,
                                      &Transform::processAddChildren);
        } else if (*itr == supportedInterfaces[1]) {
            transformNodeType.addEventIn(supportedInterfaces[1].fieldType,
                                      supportedInterfaces[1].id,
                                      &Transform::processRemoveChildren);
        } else if (*itr == supportedInterfaces[2]) {
            transformNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &Transform::processSet_center,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Transform, SFVec3f>
                                    (&Transform::center)));
        } else if (*itr == supportedInterfaces[3]) {
            transformNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &Transform::processSet_children,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Transform, MFNode>
                                    (&Transform::children)));
        } else if (*itr == supportedInterfaces[4]) {
            transformNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &Transform::processSet_rotation,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Transform, SFRotation>
                                    (&Transform::rotation)));
        } else if (*itr == supportedInterfaces[5]) {
            transformNodeType.addExposedField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                &Transform::processSet_scale,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Transform, SFVec3f>
                                    (&Transform::scale)));
        } else if (*itr == supportedInterfaces[6]) {
            transformNodeType.addExposedField(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                &Transform::processSet_scaleOrientation,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Transform, SFRotation>
                                    (&Transform::scaleOrientation)));
        } else if (*itr == supportedInterfaces[7]) {
            transformNodeType.addExposedField(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                &Transform::processSet_translation,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Transform, SFVec3f>
                                    (&Transform::translation)));
        } else if (*itr == supportedInterfaces[8]) {
            transformNodeType.addField(
                supportedInterfaces[8].fieldType,
                supportedInterfaces[8].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Transform, SFVec3f>
                                    (&Transform::bboxCenter)));
        } else if (*itr == supportedInterfaces[9]) {
            transformNodeType.addField(
                supportedInterfaces[9].fieldType,
                supportedInterfaces[9].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Transform, SFVec3f>
                                (&Transform::bboxSize)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Transform
 *
 * @brief Transform node instances.
 */

/**
 * @var Transform::TransformClass
 *
 * @brief Class object for Transform instances.
 */

/**
 * @var SFVec3f Transform::center
 *
 * @brief center exposedField.
 */

/**
 * @var SFRotation Transform::rotation
 *
 * @brief rotation exposedField.
 */

/**
 * @var SFVec3f Transform::scale
 *
 * @brief scale exposedField.
 */

/**
 * @var SFRotation Transform::scaleOrientation
 *
 * @brief scaleOrientation exposedField.
 */

/**
 * @var SFVec3f Transform::translation
 *
 * @brief translation exposedField.
 */

/**
 * @var Viewer::Object Transform::xformObject
 *
 * @brief A handle to the renderer's representation of the Transform.
 */

/**
 * @var mat4f Transform::M
 *
 * @brief Cached copy of this node's transformation.
 *
 * Currently this is used only by the culling code, but eventually
 * all the matrix manipulation needs to be moved from the Viewer
 * side over into core.
 */

/**
 * @var Transform::M_dirty
 *
 * @brief If true, we need to recalculate M.
 *
 * Is this the same as Node::d_modified? No, since it's entirely a core-side
 * issue, and has nothing to do with the viewer being out of date wrt the
 * core scene graph.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType      the NodeType associated with the instance.
 * @param scope         the Scope that the new node will belong to.
 */
Transform::Transform(const NodeType & nodeType,
                     const ScopePtr & scope):
    Node(nodeType, scope),
    ChildNode(nodeType, scope),
    GroupingNode(nodeType, scope),
    Group(nodeType, scope),
    TransformNode(nodeType, scope),
    center(vec3f(0.0, 0.0, 0.0)),
    rotation(OpenVRML::rotation(0.0, 0.0, 1.0, 0.0)),
    scale(vec3f(1.0, 1.0, 1.0)),
    scaleOrientation(OpenVRML::rotation(0.0, 0.0, 1.0, 0.0)),
    translation(vec3f(0.0, 0.0, 0.0)),
    transformDirty(true),
    xformObject(0)
{
    this->setBVolumeDirty(true);
}

/**
 * @brief Destructor.
 */
Transform::~Transform() throw ()
{
    // delete xformObject...
}

/**
 * @brief Get the transformation associated with the node as a matrix.
 *
 * @return the transformation associated with the node.
 */
const mat4f & Transform::getTransform() const throw ()
{
    this->updateTransform();
    return this->transform;
}

/**
 * @brief Render the node.
 *
 * @param viewer    a Viewer.
 * @param context   the rendering context.
 */
void Transform::render(Viewer & viewer, VrmlRenderContext context)
{
    if (context.getCullFlag() != BVolume::inside) {
        const BSphere * bs = (BSphere*)this->getBVolume();
        BSphere bv_copy(*bs);
        bv_copy.transform(context.getMatrix());
        BVolume::Intersection r = viewer.intersectViewVolume(bv_copy);
        if (context.getDrawBSpheres()) { viewer.drawBSphere(*bs, r); }

        if (r == BVolume::outside) { return; }
        if (r == BVolume::inside) { context.setCullFlag(BVolume::inside); }

        //context.setCullFlag(BVolume::BV_PARTIAL);
    }

    mat4f LM = this->getTransform();
    mat4f new_LM = context.getMatrix();
    new_LM = LM * new_LM;
    context.setMatrix(new_LM);

    if (this->xformObject && isModified()) {
        viewer.removeObject(this->xformObject);
        this->xformObject = 0;
    }

    if (this->xformObject) {
        viewer.insertReference(this->xformObject);
    } else if (!this->children.value.empty()) {
        this->xformObject = viewer.beginObject(this->getId().c_str());

        // Apply transforms
        viewer.transform(this->getTransform());
        // Render children
        this->Group::renderNoCull(viewer, context);

        viewer.endObject();
    }
    this->clearModified();
}

/**
 * @brief Get the bounding volume.
 *
 * @return the bounding volume associated with the node.
 */
const BVolume * Transform::getBVolume() const
{
    if (this->isBVolumeDirty()) {
        ((Transform*)this)->recalcBSphere();
    }
    return &this->bsphere;
}

/**
 * @brief Recalculate the bounding volume.
 */
void Transform::recalcBSphere()
{
    this->bsphere.reset();
    for (size_t i = 0; i < this->children.value.size(); ++i) {
        const NodePtr & node = this->children.value[i];
        if (node) {
            const BVolume * ci_bv = node->getBVolume();
            if (ci_bv) { this->bsphere.extend(*ci_bv); }
        }
    }
    this->bsphere.transform(this->getTransform());

    this->setBVolumeDirty(false);
}



#if 0
void
Transform::recalcBSphere()
{
  cout << "Transform[" << this << "]::recalcBSphere()" << endl;
  updateTransform();
  d_bsphere.reset();
  for (int i = 0; i<d_children.size(); ++i) {
    Node* ci = d_children[i];
    const BVolume * ci_bv = ci->getBVolume();
    if (ci_bv) { // shouldn't happen...
      BSphere * bs = (BSphere*)ci_bv;
      BSphere tmp(*bs);
      tmp.transform(M);
      d_bsphere.extend(tmp);
    }
  }
  this->setBVolumeDirty(false);
}
#endif


// P' = T � C � R � SR � S � -SR � -C � P
//
/**
 * @brief Update @a transform.
 *
 * If @a transformDirty is @c true, resynchronize the cached matrix
 * @a transform with the node fields.
 */
void Transform::updateTransform() const throw ()
{
    if (this->transformDirty) {
        this->transform = mat4f::transformation(this->translation.value,
                                                this->rotation.value,
                                                this->scale.value,
                                                this->scaleOrientation.value,
                                                this->center.value);
        this->transformDirty = false;
    }
}

/**
 * @brief set_center eventIn handler.
 *
 * @param sfvec3f   an SFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void Transform::processSet_center(const FieldValue & sfvec3f,
                                  const double timestamp)
    throw (std::bad_cast)
{
    this->center = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->setBVolumeDirty(true);
    this->transformDirty = true;
    this->emitEvent("center_changed", this->center, timestamp);
}

/**
 * @brief set_rotation eventIn handler.
 *
 * @param sfrotation    an SFRotation value.
 * @param timestamp     the current time.
 *
 * @exception std::bad_cast if @p sfrotation is not an SFRotation.
 */
void Transform::processSet_rotation(const FieldValue & sfrotation,
                                    const double timestamp)
    throw (std::bad_cast)
{
    this->rotation = dynamic_cast<const SFRotation &>(sfrotation);
    this->setModified();
    this->setBVolumeDirty(true);
    this->transformDirty = true;
    this->emitEvent("rotation_changed", this->rotation, timestamp);
}

/**
 * @brief set_scale eventIn handler.
 *
 * @param sfvec3f   an SFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void Transform::processSet_scale(const FieldValue & sfvec3f,
                                 const double timestamp)
    throw (std::bad_cast)
{
    this->scale = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->setBVolumeDirty(true);
    this->transformDirty = true;
    this->emitEvent("scale_changed", this->scale, timestamp);
}

/**
 * @brief set_scaleOrientation eventIn handler.
 *
 * @param sfrotation    an SFRotation value.
 * @param timestamp     the current time.
 *
 * @exception std::bad_cast if @p sfrotation is not an SFRotation.
 */
void Transform::processSet_scaleOrientation(const FieldValue & sfrotation,
                                            const double timestamp)
    throw (std::bad_cast)
{
    this->scaleOrientation = dynamic_cast<const SFRotation &>(sfrotation);
    this->setModified();
    this->setBVolumeDirty(true);
    this->transformDirty = true;
    this->emitEvent("scaleOrientation_changed", this->scaleOrientation,
                    timestamp);
}

/**
 * @brief set_translation eventIn handler.
 *
 * @param sfvec3f   an SFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void Transform::processSet_translation(const FieldValue & sfvec3f,
                                       const double timestamp)
    throw (std::bad_cast)
{
    this->translation = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->setBVolumeDirty(true);
    this->transformDirty = true;
    this->emitEvent("translation_changed", this->translation, timestamp);
}


/**
 * @class ViewpointClass
 *
 * @brief Class object for Viewpoint nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
ViewpointClass::ViewpointClass(Browser & browser):
    NodeClass(browser),
    first(0)
{}

/**
 * @brief Destructor.
 */
ViewpointClass::~ViewpointClass() throw ()
{}

/**
 * @brief Set the first Viewpoint node in the world.
 *
 * The first Viewpoint node in the world is used as the initial viewpoint.
 * This method is used by Viewpoint::do_initialize.
 *
 * @param viewpoint    a Viewpoint node.
 */
void ViewpointClass::setFirst(Viewpoint & viewpoint) throw ()
{
    this->first = &viewpoint;
}

/**
 * @brief Check to see if the first node has been set.
 *
 * This method is used by Viewpoint::do_initialize.
 *
 * @return @c true if the first node has already been set; @c false otherwise.
 */
bool ViewpointClass::hasFirst() const throw ()
{
    return this->first;
}

/**
 * @brief Push a Viewpoint on the top of the bound node stack.
 *
 * @param viewpoint    the node to bind.
 * @param timestamp the current time.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void ViewpointClass::bind(Viewpoint & viewpoint, const double timestamp)
    throw (std::bad_alloc)
{
    //
    // If the node is already the active node, do nothing.
    //
    if (!this->boundNodes.empty() && &viewpoint == this->boundNodes.back()) {
        return;
    }

    //
    // If the node is already on the stack, remove it.
    //
    const BoundNodes::iterator pos =
        std::find(this->boundNodes.begin(), this->boundNodes.end(), &viewpoint);
    if (pos != this->boundNodes.end()) { this->boundNodes.erase(pos); }

    //
    // Send FALSE from the currently active node's isBound.
    //
    if (!this->boundNodes.empty()) {
        Viewpoint & current =
                dynamic_cast<Viewpoint &>(*this->boundNodes.back());
        current.bound.value = false;
        current.emitEvent("isBound", current.bound, timestamp);
    }

    //
    // Push the node to the top of the stack, and have it send isBound TRUE.
    //
    this->boundNodes.push_back(&viewpoint);
    viewpoint.bound.value = true;
    viewpoint.emitEvent("isBound", viewpoint.bound, timestamp);

    this->browser.setActiveViewpoint(viewpoint);
}

/**
 * @brief Remove a Viewpoint from the bound node stack.
 *
 * @param viewpoint    the node to unbind.
 * @param timestamp     the current time.
 */
void ViewpointClass::unbind(Viewpoint & viewpoint, const double timestamp)
    throw ()
{
    const BoundNodes::iterator pos =
        std::find(this->boundNodes.begin(), this->boundNodes.end(), &viewpoint);
    if (pos != this->boundNodes.end()) {
        viewpoint.bound.value = false;
        viewpoint.emitEvent("isBound", viewpoint.bound, timestamp);

        if (pos == this->boundNodes.end() - 1
                && this->boundNodes.size() > 1) {
            Viewpoint & newActive =
                    dynamic_cast<Viewpoint &>(**(this->boundNodes.end() - 2));
            newActive.bound.value = true;
            newActive.emitEvent("isBound", newActive.bound, timestamp);

            this->browser.setActiveViewpoint(viewpoint);
        } else {
            this->browser.resetDefaultViewpoint();
        }
        this->boundNodes.erase(pos);
    }
}

/**
 * @brief NodeClass-specific initialization.
 *
 * @param initialViewpoint  the ViewpointNode that should be bound initially.
 * @param timestamp         the current time.
 */
void ViewpointClass::initialize(ViewpointNode * initialViewpoint,
                                const double timestamp)
    throw ()
{
    if (!initialViewpoint) { initialViewpoint = this->first; }
    if (initialViewpoint) {
        initialViewpoint->processEvent("set_bind", SFBool(true), timestamp);
    }
}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating Viewpoint nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                                  supported by ViewpointClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
ViewpointClass::createType(const std::string & id,
                           const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::eventIn, FieldValue::sfbool, "set_bind"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sffloat, "fieldOfView"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "jump"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfrotation, "orientation"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "position"),
        NodeInterface(NodeInterface::field, FieldValue::sfstring, "description"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sftime, "bindTime"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isBound")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<Viewpoint>(*this, id));
    Vrml97NodeTypeImpl<Viewpoint> & viewpointNodeType =
            static_cast<Vrml97NodeTypeImpl<Viewpoint> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<Viewpoint>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            viewpointNodeType.addEventIn(supportedInterfaces[0].fieldType,
                                   supportedInterfaces[0].id,
                                   &Viewpoint::processSet_bind);
        } else if (*itr == supportedInterfaces[1]) {
            viewpointNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &Viewpoint::processSet_fieldOfView,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Viewpoint, SFFloat>
                                    (&Viewpoint::fieldOfView)));
        } else if (*itr == supportedInterfaces[2]) {
            viewpointNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &Viewpoint::processSet_jump,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Viewpoint, SFBool>
                                    (&Viewpoint::jump)));
        } else if (*itr == supportedInterfaces[3]) {
            viewpointNodeType.addExposedField(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                &Viewpoint::processSet_orientation,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Viewpoint, SFRotation>
                                    (&Viewpoint::orientation)));
        } else if (*itr == supportedInterfaces[4]) {
            viewpointNodeType.addExposedField(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                &Viewpoint::processSet_position,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Viewpoint, SFVec3f>
                                    (&Viewpoint::position)));
        } else if (*itr == supportedInterfaces[5]) {
            viewpointNodeType.addField(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Viewpoint, SFString>
                                    (&Viewpoint::description)));
        } else if (*itr == supportedInterfaces[6]) {
            viewpointNodeType.addEventOut(
                supportedInterfaces[6].fieldType,
                supportedInterfaces[6].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Viewpoint, SFTime>
                                    (&Viewpoint::bindTime)));
        } else if (*itr == supportedInterfaces[7]) {
            viewpointNodeType.addEventOut(
                supportedInterfaces[7].fieldType,
                supportedInterfaces[7].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<Viewpoint, SFBool>
                                    (&Viewpoint::bound)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class Viewpoint
 *
 * @todo need copy constructor for d_parentTransform ...
 */

/**
 * @var Viewpoint::ViewpointClass
 *
 * @brief Class object for Viewpoint instances.
 */

/**
 * @var SFFloat Viewpoint::fieldOfView
 *
 * @brief fieldOfView exposedField.
 */

/**
 * @var SFBool Viewpoint::jump
 *
 * @brief jump exposedField.
 */

/**
 * @var SFRotation Viewpoint::orientation
 *
 * @brief orientation exposedField.
 */

/**
 * @var SFVec3f Viewpoint::position
 *
 * @brief position exposedField.
 */

/**
 * @var SFString Viewpoint::description
 *
 * @brief description field.
 */

/**
 * @var SFBool Viewpoint::bound
 *
 * @brief isBound eventOut.
 */

/**
 * @var SFTime Viewpoint::bindTime
 *
 * @brief bindTime eventOut.
 */

/**
 * @var Node * Viewpoint::parentTransform
 *
 * @brief A pointer to the parent Transform for this node.
 */

namespace {
    const float DEFAULT_FIELD_OF_VIEW = 0.785398;
}

/**
 * @brief Constructor.
 *
 * @param nodeType      the NodeType associated with the instance.
 * @param scope         the Scope that the new node will belong to.
 */
Viewpoint::Viewpoint(const NodeType & nodeType,
                     const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractBase(nodeType, scope),
    ChildNode(nodeType, scope),
    ViewpointNode(nodeType, scope),
    fieldOfView(DEFAULT_FIELD_OF_VIEW),
    jump(true),
    orientation(rotation(0.0, 0.0, 1.0, 0.0)),
    position(vec3f(0.0, 0.0, 10.0)),
    bound(false),
    bindTime(0),
    finalTransformationDirty(true)
{}

/**
 * @brief Destructor.
 */
Viewpoint::~Viewpoint() throw ()
{}

/**
 * @brief Get the transformation of the ViewpointNode in the global coordinate
 *      system.
 *
 * @return the transformation of the ViewpointNode in the global coordinate
 *      system.
 */
const mat4f & Viewpoint::getTransformation() const throw ()
{
    this->updateFinalTransformation();
    return this->finalTransformation;
}

/**
 * @brief Get the transformation of the user view relative to the
 *      ViewpointNode.
 *
 * @return the transformation of the user view relative to the ViewpointNode.
 */
const mat4f & Viewpoint::getUserViewTransform() const throw ()
{
    return this->userViewTransform;
}

/**
 * @brief Set the transformation of the user view relative to the
 *      ViewpointNode.
 *
 * @param transform the new transformation.
 */
void Viewpoint::setUserViewTransform(const mat4f & transform) throw ()
{
    this->userViewTransform = transform;
}

/**
 * @brief description accessor.
 *
 * @return the description.
 */
const std::string & Viewpoint::getDescription() const throw ()
{
    return this->description.value;
}

/**
 * @brief Get the field of view.
 *
 * @return the field of view in radians.
 */
float Viewpoint::getFieldOfView() const throw ()
{
    return this->fieldOfView.value;
}

/**
 * @todo Implement me!
 */
void Viewpoint::getFrustum(VrmlFrustum& frust) const
{
    // XXX Implement me!
}

/**
 * @brief Get the bounding volume.
 *
 * @return the bounding volume associated with the node.
 */
const BVolume * Viewpoint::getBVolume() const
{
    static BSphere * inf_bsphere = 0;
    if (!inf_bsphere) { inf_bsphere = new BSphere(); }
    return inf_bsphere;
}

/**
 * @brief orientation accessor.
 *
 * @return the orientation.
 */
const SFRotation & Viewpoint::getOrientation() const
{
    return this->orientation;
}

/**
 * @brief position accessor.
 *
 * @return the position.
 */
const SFVec3f & Viewpoint::getPosition() const
{
    return this->position;
}

/**
 * @brief Initialize.
 *
 * @param timestamp the current time.
 */
void Viewpoint::do_initialize(const double timestamp) throw ()
{
    assert(this->getScene());
    this->getScene()->browser.addViewpoint(*this);
    assert(dynamic_cast<ViewpointClass *>(&this->nodeType.nodeClass));
    ViewpointClass & nodeClass =
            static_cast<ViewpointClass &>(this->nodeType.nodeClass);
    if (!nodeClass.hasFirst()) { nodeClass.setFirst(*this); }
}

namespace {

    struct AccumulateTransform : std::unary_function<const Node *, void> {
        explicit AccumulateTransform(mat4f & transform) throw ():
            transform(&transform)
        {}

        void operator()(const Node * node) const throw ()
        {
            assert(node);
            const TransformNode * const transformNode = node->toTransform();
            if (transformNode) {
                *this->transform =
                        transformNode->getTransform() * *this->transform;
            }
        }

    private:
        mat4f * transform;
    };
}

/**
 * @brief Relocate.
 *
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Viewpoint::do_relocate() throw (std::bad_alloc)
{
    assert(this->getScene());
    const NodePath path = this->getScene()->browser.findNode(*this);
    assert(!path.empty());
    this->parentTransform = mat4f();
    std::for_each(path.begin(), path.end(),
                  AccumulateTransform(this->parentTransform));
    this->finalTransformationDirty = true;
}

/**
 * @brief Shut down.
 *
 * @param timestamp the current time.
 */
void Viewpoint::do_shutdown(const double timestamp) throw ()
{
    assert(this->getScene());
    this->getScene()->browser.removeViewpoint(*this);
}

/**
 * @brief set_bind eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfbool is not an SFBool value.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Viewpoint::processSet_bind(const FieldValue & sfbool,
                                const double timestamp)
    throw (std::bad_cast, std::bad_alloc)
{
    const SFBool & value = dynamic_cast<const SFBool &>(sfbool);
    assert(dynamic_cast<ViewpointClass *>(&this->nodeType.nodeClass));
    ViewpointClass & nodeClass =
            static_cast<ViewpointClass &>(this->nodeType.nodeClass);
    if (value.value) {
        nodeClass.bind(*this, timestamp);
    } else {
        nodeClass.unbind(*this, timestamp);
    }
}

/**
 * @brief set_fieldOfView eventIn handler.
 *
 * @param sffloat   an SFFloat value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sffloat is not an SFFloat value.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Viewpoint::processSet_fieldOfView(const FieldValue & sffloat,
                                       const double timestamp)
    throw (std::bad_cast)
{
    this->fieldOfView = dynamic_cast<const SFFloat &>(sffloat);
    this->setModified();
    this->emitEvent("fieldOfView_changed", this->fieldOfView, timestamp);
}

/**
 * @brief set_jump eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfbool is not an SFBool value.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Viewpoint::processSet_jump(const FieldValue & sfbool,
                                const double timestamp)
    throw (std::bad_cast)
{
    this->jump = dynamic_cast<const SFBool &>(sfbool);
    this->setModified();
    this->emitEvent("jump_changed", this->jump, timestamp);
}

/**
 * @brief set_orientation eventIn handler.
 *
 * @param sfrotation    an SFRotation value.
 * @param timestamp     the current time.
 *
 * @exception std::bad_cast     if @p sfrotation is not an SFRotation value.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Viewpoint::processSet_orientation(const FieldValue & sfrotation,
                                       const double timestamp)
    throw (std::bad_cast)
{
    this->orientation = dynamic_cast<const SFRotation &>(sfrotation);
    this->setModified();
    this->finalTransformationDirty = true;
    this->emitEvent("orientation_changed", this->orientation, timestamp);
}

/**
 * @brief set_position eventIn handler.
 *
 * @param sfvec3f   an SFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast     if @p sfvec3f is not an SFVec3f value.
 * @exception std::bad_alloc    if memory allocation fails.
 */
void Viewpoint::processSet_position(const FieldValue & sfvec3f,
                                    const double timestamp)
    throw (std::bad_cast)
{
    this->position = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->finalTransformationDirty = true;
    this->emitEvent("position_changed", this->position, timestamp);
}

void Viewpoint::updateFinalTransformation() const throw ()
{
    if (this->finalTransformationDirty) {
        static const vec3f scale(1.0, 1.0, 1.0);
        static const rotation scaleOrientation;
        static const vec3f center;
        const mat4f & t = mat4f::transformation(this->position.value,
                                                this->orientation.value,
                                                scale,
                                                scaleOrientation,
                                                center);
        this->finalTransformation = t * this->parentTransform;
        this->finalTransformationDirty = false;
    }
}


/**
 * @class VisibilitySensorClass
 *
 * @brief Class object for VisibilitySensor nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
VisibilitySensorClass::VisibilitySensorClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
VisibilitySensorClass::~VisibilitySensorClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating VisibilitySensor
 *      nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                                  supported by VisibilitySensorClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr
VisibilitySensorClass::createType(const std::string & id,
                                  const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "center"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfbool, "enabled"),
        NodeInterface(NodeInterface::exposedField, FieldValue::sfvec3f, "size"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sftime, "enterTime"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sftime, "exitTime"),
        NodeInterface(NodeInterface::eventOut, FieldValue::sfbool, "isActive")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<VisibilitySensor>(*this, id));
    Vrml97NodeTypeImpl<VisibilitySensor> & visibilitySensorNodeType =
            static_cast<Vrml97NodeTypeImpl<VisibilitySensor> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<VisibilitySensor>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            visibilitySensorNodeType.addExposedField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                &VisibilitySensor::processSet_center,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<VisibilitySensor, SFVec3f>
                                    (&VisibilitySensor::center)));
        } else if (*itr == supportedInterfaces[1]) {
            visibilitySensorNodeType.addExposedField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                &VisibilitySensor::processSet_enabled,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<VisibilitySensor, SFBool>
                                    (&VisibilitySensor::enabled)));
        } else if (*itr == supportedInterfaces[2]) {
            visibilitySensorNodeType.addExposedField(
                supportedInterfaces[2].fieldType,
                supportedInterfaces[2].id,
                &VisibilitySensor::processSet_size,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<VisibilitySensor, SFVec3f>
                                    (&VisibilitySensor::size)));
        } else if (*itr == supportedInterfaces[3]) {
            visibilitySensorNodeType.addEventOut(
                supportedInterfaces[3].fieldType,
                supportedInterfaces[3].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<VisibilitySensor, SFTime>
                                    (&VisibilitySensor::enterTime)));
        } else if (*itr == supportedInterfaces[4]) {
            visibilitySensorNodeType.addEventOut(
                supportedInterfaces[4].fieldType,
                supportedInterfaces[4].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<VisibilitySensor, SFTime>
                                    (&VisibilitySensor::exitTime)));
        } else if (*itr == supportedInterfaces[5]) {
            visibilitySensorNodeType.addEventOut(
                supportedInterfaces[5].fieldType,
                supportedInterfaces[5].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<VisibilitySensor, SFBool>
                                    (&VisibilitySensor::active)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class VisibilitySensor
 *
 * @brief VisibilitySensor node instances.
 */

/**
 * @var VisibilitySensor::VisibilitySensorClass
 *
 * @brief Class object for VisibilitySensor instances.
 */

/**
 * @var VisibilitySensor::center
 *
 * @brief center exposedField.
 */

/**
 * @var VisibilitySensor::enabled
 *
 * @brief enabled exposedField.
 */

/**
 * @var VisibilitySensor::size
 *
 * @brief size exposedField.
 */

/**
 * @var VisibilitySensor::active
 *
 * @brief isActive eventOut.
 */

/**
 * @var VisibilitySensor::enterTime
 *
 * @brief enterTime eventOut.
 */

/**
 * @var VisibilitySensor::exitTime
 *
 * @brief exitTime eventOut.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType      the NodeType associated with the instance.
 * @param scope         the Scope that the new node will belong to.
 */
VisibilitySensor::VisibilitySensor(const NodeType & nodeType,
                                   const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope),
    center(vec3f(0.0, 0.0, 0.0)),
    enabled(true),
    size(vec3f(0.0, 0.0, 0.0)),
    active(false),
    enterTime(0.0),
    exitTime(0.0)
{
    this->setModified();
}

/**
 * @brief Destructor.
 */
VisibilitySensor::~VisibilitySensor() throw ()
{}

/**
 * @brief Generate visibility events.
 *
 * This is in a render() method since the it needs to be computed
 * with respect to the accumulated transformations above it in the
 * scene graph. Move to update() when xforms are accumulated in Groups...
 */
void VisibilitySensor::render(Viewer & viewer, const VrmlRenderContext context)
{
    using OpenVRML_::fpzero;

    if (this->enabled.value) {
        SFTime timeNow(Browser::getCurrentTime());
        float xyz[2][3];

        // hack: enclose box in a sphere...
        xyz[0][0] = this->center.value.x();
        xyz[0][1] = this->center.value.y();
        xyz[0][2] = this->center.value.z();
        xyz[1][0] = this->center.value.x() + this->size.value.x();
        xyz[1][1] = this->center.value.y() + this->size.value.y();
        xyz[1][2] = this->center.value.z() + this->size.value.z();
        viewer.transformPoints(2, &xyz[0][0]);
        float dx = xyz[1][0] - xyz[0][0];
        float dy = xyz[1][1] - xyz[0][1];
        float dz = xyz[1][2] - xyz[0][2];
        float r  = dx * dx + dy * dy + dz * dz;
        if (!fpzero(r)) { r = sqrt(r); }

        // Was the sphere visible last time through? How does this work
        // for USE'd nodes? I need a way for each USE to store whether
        // it was active.
        bool wasIn = this->active.value;

        // Is the sphere visible? ...
        bool inside = xyz[0][2] < 0.0; // && z > - scene->visLimit()
        if (inside) {
            NavigationInfo * ni = this->nodeType.nodeClass.browser
                                    .bindableNavigationInfoTop();
            if (ni && !fpzero(ni->getVisibilityLimit())
                    && xyz[0][2] < -(ni->getVisibilityLimit())) {
                inside = false;
            }
        }

        // This bit assumes 90degree fieldOfView to get rid of trig calls...
        if (inside) {
            inside = fabs(xyz[0][0]) < -0.5 * xyz[0][2] + r
                    && fabs(xyz[0][1]) < -0.5 * xyz[0][2] + r;
        }

        // Just became visible
        if (inside && !wasIn) {
            this->active.value = true;
            this->emitEvent("isActive", this->active, timeNow.value);

            this->enterTime = timeNow;
            this->emitEvent("enterTime", this->enterTime, timeNow.value);
        }

        // Check if viewer has left the box
        else if (wasIn && !inside) {
            this->active.value = false;
            this->emitEvent("isActive", this->active, timeNow.value);

            this->exitTime = timeNow;
            this->emitEvent("exitTime", this->exitTime, timeNow.value);
        }
    } else {
        this->clearModified();
    }
}

/**
 * @brief set_center eventIn handler.
 *
 * @param sfvec3f   an SFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void VisibilitySensor::processSet_center(const FieldValue & sfvec3f,
                                         const double timestamp)
    throw (std::bad_cast)
{
    this->center = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("center_changed", this->center, timestamp);
}

/**
 * @brief set_enabled eventIn handler.
 *
 * @param sfbool    an SFBool value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfbool is not an SFBool.
 */
void VisibilitySensor::processSet_enabled(const FieldValue & sfbool,
                                          double timestamp)
    throw (std::bad_cast)
{
    this->enabled = dynamic_cast<const SFBool &>(sfbool);
    this->setModified();
    this->emitEvent("enabled_changed", this->enabled, timestamp);
}

/**
 * @brief set_size eventIn handler.
 *
 * @param sfvec3f   an SFVec3f value.
 * @param timestamp the current time.
 *
 * @exception std::bad_cast if @p sfvec3f is not an SFVec3f.
 */
void VisibilitySensor::processSet_size(const FieldValue & sfvec3f,
                                       const double timestamp)
    throw (std::bad_cast)
{
    this->size = dynamic_cast<const SFVec3f &>(sfvec3f);
    this->setModified();
    this->emitEvent("size_changed", this->size, timestamp);
}


/**
 * @class WorldInfoClass
 *
 * @brief Class object for WorldInfo nodes.
 */

/**
 * @brief Constructor.
 *
 * @param browser the Browser associated with this NodeClass.
 */
WorldInfoClass::WorldInfoClass(Browser & browser):
    NodeClass(browser)
{}

/**
 * @brief Destructor.
 */
WorldInfoClass::~WorldInfoClass() throw ()
{}

/**
 * @brief Create a NodeType.
 *
 * @param id            the name for the new NodeType.
 * @param interfaces    the interfaces for the new NodeType.
 *
 * @return a NodeTypePtr to a NodeType capable of creating WorldInfo nodes.
 *
 * @exception UnsupportedInterface  if @p interfaces includes an interface not
 *                                  supported by WorldInfoClass.
 * @exception std::bad_alloc        if memory allocation fails.
 */
const NodeTypePtr WorldInfoClass::createType(const std::string & id,
                                        const NodeInterfaceSet & interfaces)
    throw (UnsupportedInterface, std::bad_alloc)
{
    static const NodeInterface supportedInterfaces[] = {
        NodeInterface(NodeInterface::field, FieldValue::mfstring, "info"),
        NodeInterface(NodeInterface::field, FieldValue::sfstring, "title")
    };
    const NodeTypePtr nodeType(new Vrml97NodeTypeImpl<WorldInfo>(*this, id));
    Vrml97NodeTypeImpl<WorldInfo> & worldInfoNodeType =
            static_cast<Vrml97NodeTypeImpl<WorldInfo> &>(*nodeType);
    typedef Vrml97NodeTypeImpl<WorldInfo>::NodeFieldPtrPtr NodeFieldPtrPtr;
    for (NodeInterfaceSet::const_iterator itr(interfaces.begin());
            itr != interfaces.end(); ++itr) {
        if (*itr == supportedInterfaces[0]) {
            worldInfoNodeType.addField(
                supportedInterfaces[0].fieldType,
                supportedInterfaces[0].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<WorldInfo, MFString>
                                    (&WorldInfo::info)));
        } else if (*itr == supportedInterfaces[1]) {
            worldInfoNodeType.addField(
                supportedInterfaces[1].fieldType,
                supportedInterfaces[1].id,
                NodeFieldPtrPtr(new NodeFieldPtrImpl<WorldInfo, SFString>
                                    (&WorldInfo::title)));
        } else {
            throw UnsupportedInterface("Invalid interface.");
        }
    }
    return nodeType;
}

/**
 * @class WorldInfo
 *
 * @brief WorldInfo node instances.
 */

/**
 * @var WorldInfo::WorldInfoClass
 *
 * @brief Class object for WorldInfo instances.
 */

/**
 * @var WorldInfo::info
 *
 * @brief string field.
 */

/**
 * @var WorldInfo::title
 *
 * @brief title field.
 */

/**
 * @brief Constructor.
 *
 * @param nodeType      the NodeType associated with the instance.
 * @param scope         the Scope that the new node will belong to.
 */
WorldInfo::WorldInfo(const NodeType & nodeType,
                     const ScopePtr & scope):
    Node(nodeType, scope),
    AbstractChild(nodeType, scope)
{}

/**
 * @brief Destructor.
 */
WorldInfo::~WorldInfo() throw ()
{}

} // namespace Vrml97Node

} // namespace OpenVRML
