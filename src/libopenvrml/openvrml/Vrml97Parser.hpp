#ifndef INC_Vrml97Parser_hpp_
#define INC_Vrml97Parser_hpp_

#include <antlr/config.hpp>
/* $ANTLR 2.7.2: "Vrml97Parser.g" -> "Vrml97Parser.hpp"$ */
#include <antlr/TokenStream.hpp>
#include <antlr/TokenBuffer.hpp>
#include "Vrml97ParserTokenTypes.hpp"
#include <antlr/LLkParser.hpp>

#line 22 "Vrml97Parser.g"

# include <memory>
# include "field.h"
# include "node_ptr.h"
# define ANTLR_LBRACE {
# define ANTLR_RBRACE }

namespace openvrml {
    class scope;
    class script_node;
    class node_type;
}

namespace {
    class Vrml97Scanner : public antlr::TokenStream {
    public:
        static const int EOF_;
        //
        // The following identifiers for constants must match those in the file
        // Vrml97TokenTypes.txt.
        //
        static const int PERIOD;
        static const int LBRACKET;
        static const int RBRACKET;
        static const int LBRACE;
        static const int RBRACE;
        static const int ID;
        static const int INTEGER;
        static const int HEX_INTEGER;
        static const int REAL;
        static const int STRING;
        static const int KEYWORD_DEF;
        static const int KEYWORD_EVENTIN;
        static const int KEYWORD_EVENTOUT;
        static const int KEYWORD_EXPOSEDFIELD;
        static const int KEYWORD_EXTERNPROTO;
        static const int KEYWORD_FALSE;
        static const int KEYWORD_FIELD;
        static const int KEYWORD_IS;
        static const int KEYWORD_NULL;
        static const int KEYWORD_PROTO;
        static const int KEYWORD_ROUTE;
        static const int KEYWORD_TO;
        static const int KEYWORD_TRUE;
        static const int KEYWORD_USE;
        static const int FIELDTYPE_SFBOOL;
        static const int FIELDTYPE_SFCOLOR;
        static const int FIELDTYPE_SFFLOAT;
        static const int FIELDTYPE_SFIMAGE;
        static const int FIELDTYPE_SFINT32;
        static const int FIELDTYPE_SFNODE;
        static const int FIELDTYPE_SFROTATION;
        static const int FIELDTYPE_SFSTRING;
        static const int FIELDTYPE_SFTIME;
        static const int FIELDTYPE_SFVEC2F;
        static const int FIELDTYPE_SFVEC3F;
        static const int FIELDTYPE_MFCOLOR;
        static const int FIELDTYPE_MFFLOAT;
        static const int FIELDTYPE_MFINT32;
        static const int FIELDTYPE_MFNODE;
        static const int FIELDTYPE_MFROTATION;
        static const int FIELDTYPE_MFSTRING;
        static const int FIELDTYPE_MFTIME;
        static const int FIELDTYPE_MFVEC2F;
        static const int FIELDTYPE_MFVEC3F;

        Vrml97Scanner(std::istream &);

        virtual antlr::RefToken nextToken();

    private:
        void getNextChar();
        void identifyKeyword(antlr::Token &);
        void identifyFieldType(antlr::Token &);
        void identifyTerminalSymbol(antlr::Token &);

        std::istream & in_;
        size_t      line_;
        size_t      col_;
        int         c_;
        int         prev_char_;
        int         prev_token_type_;
        bool        read_too_much_;
        bool        expecting_field_type_;
    };

    //
    // Per-node list of IS mappings. A multimap is used for no other reason
    // than that redundancies are checked later.
    //
    typedef std::multimap<std::string, std::string> is_list;
}

namespace openvrml ANTLR_LBRACE

#line 108 "Vrml97Parser.hpp"
class Vrml97Parser : public ANTLR_USE_NAMESPACE(antlr)LLkParser, public Vrml97ParserTokenTypes
{
#line 606 "Vrml97Parser.g"

public:
    Vrml97Parser(antlr::TokenStream & lexer, const std::string & uri):
        antlr::LLkParser(lexer, 1),
        uri(uri)
    {}

private:
    const std::string uri;
#line 112 "Vrml97Parser.hpp"
public:
	void initializeASTFactory( ANTLR_USE_NAMESPACE(antlr)ASTFactory& factory );
protected:
	Vrml97Parser(ANTLR_USE_NAMESPACE(antlr)TokenBuffer& tokenBuf, int k);
public:
	Vrml97Parser(ANTLR_USE_NAMESPACE(antlr)TokenBuffer& tokenBuf);
protected:
	Vrml97Parser(ANTLR_USE_NAMESPACE(antlr)TokenStream& lexer, int k);
public:
	Vrml97Parser(ANTLR_USE_NAMESPACE(antlr)TokenStream& lexer);
	Vrml97Parser(const ANTLR_USE_NAMESPACE(antlr)ParserSharedInputState& state);
	int getNumTokens() const
	{
		return Vrml97Parser::NUM_TOKENS;
	}
	const char* getTokenName( int type ) const
	{
		if( type > getNumTokens() ) return 0;
		return Vrml97Parser::tokenNames[type];
	}
	const char* const* getTokenNames() const
	{
		return Vrml97Parser::tokenNames;
	}
	public: void vrmlScene(
		openvrml::browser & browser,
          std::vector<node_ptr> & nodes
	);
	public: void statement(
		openvrml::browser & browser,
          std::vector<node_ptr> & nodes,
          const scope_ptr & scope
	);
	public: node_ptr  nodeStatement(
		openvrml::browser & browser, const scope_ptr & scope
	);
	public: void protoStatement(
		openvrml::browser & browser, const scope_ptr & scope
	);
	public: void routeStatement(
		const openvrml::scope & scope
	);
	public: node_ptr  node(
		openvrml::browser & browser,
     const scope_ptr & scope,
     const std::string & nodeId
	);
	public: void externproto(
		openvrml::browser & browser, const openvrml::scope_ptr & scope
	);
	public: void proto(
		openvrml::browser & browser, const scope_ptr & scope
	);
	public: void protoInterfaceDeclaration(
		
    openvrml::browser & browser,
    const scope_ptr & outer_scope,
    const std::string & proto_id,
    node_interface_set & interfaces,
    proto_node_class::default_value_map_t & default_value_map
	);
	public: void protoBody(
		openvrml::browser & browser,
          const scope_ptr & scope,
          const node_interface_set & interfaces,
          std::vector<node_ptr> & impl_nodes,
          proto_node_class::is_map_t & is_map,
          proto_node_class::routes_t & routes
	);
	public: node_interface::type_id  eventInterfaceType();
	public: openvrml::field_value::type_id  fieldType();
	public: node_interface::type_id  fieldInterfaceType();
	public: openvrml::field_value_ptr  fieldValue(
		openvrml::browser & browser,
           const openvrml::scope_ptr & scope,
           openvrml::field_value::type_id ft
	);
	public: node_ptr  protoNodeStatement(
		openvrml::browser & browser,
                   const openvrml::scope_ptr & scope,
                   const node_interface_set & proto_interfaces,
                   proto_node_class::is_map_t & is_map,
                   proto_node_class::routes_t & routes
	);
	public: void protoBodyStatement(
		openvrml::browser & browser,
                   const scope_ptr & scope,
                   const node_interface_set & interfaces,
                   std::vector<node_ptr> & impl_nodes,
                   proto_node_class::is_map_t & is_map,
                   proto_node_class::routes_t & routes
	);
	public: void protoRouteStatement(
		const openvrml::scope & scope,
                    proto_node_class::routes_t & routes
	);
	public: node_ptr  protoNode(
		openvrml::browser & browser,
          const scope_ptr & scope,
          const node_interface_set & proto_interfaces,
          proto_node_class::is_map_t & is_map,
          proto_node_class::routes_t & routes,
          const std::string & nodeId
	);
	public: void externInterfaceDeclaration(
		openvrml::node_interface_set & interfaces
	);
	public: mfstring  externprotoUrlList();
	public: node_interface::type_id  interfaceType();
	public: std::string  stringValue();
	public: void nodeBodyElement(
		browser & b,
                const scope_ptr & scope,
                const node_interface_set & interfaces,
                initial_value_map & initial_values
	);
	public: void scriptInterfaceDeclaration(
		browser & b,
                           const scope_ptr & scope,
                           node_interface_set & interfaces,
                           initial_value_map & initial_values
	);
	public: void scriptFieldInterfaceDeclaration(
		browser & b,
                                const scope_ptr & scope,
                                node_interface_set & interfaces,
                                initial_value_map & initial_values
	);
	public: void protoNodeBodyElement(
		openvrml::browser & browser,
                     const scope_ptr & scope,
                     const node_interface_set & proto_interfaces,
                     proto_node_class::is_map_t & is_map,
                     proto_node_class::routes_t & routes,
                     const node_interface_set & node_interfaces,
                     initial_value_map & initial_values,
                     is_list & is_mappings
	);
	public: void protoScriptInterfaceDeclaration(
		openvrml::browser & browser,
                                const openvrml::scope_ptr & scope,
                                const node_interface_set & proto_interfaces,
                                proto_node_class::is_map_t & is_map,
                                proto_node_class::routes_t & routes,
                                node_interface_set & interfaces,
                                initial_value_map & initial_values,
                                is_list & is_mappings
	);
	public: field_value_ptr  protoFieldValue(
		openvrml::browser & browser,
                const scope_ptr & scope,
                const node_interface_set & proto_interfaces,
                proto_node_class::is_map_t & is_map,
                proto_node_class::routes_t & routes,
                const field_value::type_id ft
	);
	public: void isStatement(
		const std::string & impl_node_interface_id,
            is_list & is_mappings
	);
	public: void protoScriptFieldInterfaceDeclaration(
		
    openvrml::browser & browser,
    const scope_ptr & scope,
    const node_interface_set & proto_interfaces,
    proto_node_class::is_map_t & is_map,
    proto_node_class::routes_t & routes,
    node_interface_set & interfaces,
    initial_value_map & initial_values,
    is_list & is_mappings
	);
	public: openvrml::field_value_ptr  nodeFieldValue(
		openvrml::browser & browser,
               const openvrml::scope_ptr & scope,
               openvrml::field_value::type_id ft
	);
	public: openvrml::field_value_ptr  nonNodeFieldValue(
		openvrml::field_value::type_id ft
	);
	public: field_value_ptr  protoNodeFieldValue(
		openvrml::browser & browser,
                    const scope_ptr & scope,
                    const node_interface_set & proto_interfaces,
                    proto_node_class::is_map_t & is_map,
                    proto_node_class::routes_t & routes,
                    field_value::type_id ft
	);
	public: openvrml::field_value_ptr  sfBoolValue();
	public: openvrml::field_value_ptr  sfColorValue();
	public: openvrml::field_value_ptr  sfFloatValue();
	public: openvrml::field_value_ptr  sfImageValue();
	public: openvrml::field_value_ptr  sfInt32Value();
	public: openvrml::field_value_ptr  sfRotationValue();
	public: openvrml::field_value_ptr  sfStringValue();
	public: openvrml::field_value_ptr  sfTimeValue();
	public: openvrml::field_value_ptr  sfVec2fValue();
	public: openvrml::field_value_ptr  sfVec3fValue();
	public: openvrml::field_value_ptr  mfColorValue();
	public: openvrml::field_value_ptr  mfFloatValue();
	public: openvrml::field_value_ptr  mfInt32Value();
	public: field_value_ptr  mfRotationValue();
	public: openvrml::field_value_ptr  mfStringValue();
	public: openvrml::field_value_ptr  mfTimeValue();
	public: openvrml::field_value_ptr  mfVec2fValue();
	public: openvrml::field_value_ptr  mfVec3fValue();
	public: openvrml::field_value_ptr  sfNodeValue(
		openvrml::browser & browser,
            const openvrml::scope_ptr & scope
	);
	public: openvrml::field_value_ptr  mfNodeValue(
		openvrml::browser & browser,
            const openvrml::scope_ptr & scope
	);
	public: field_value_ptr  protoSfNodeValue(
		openvrml::browser & browser,
                 const scope_ptr & scope,
                 const node_interface_set & proto_interfaces,
                 proto_node_class::is_map_t & is_map,
                 proto_node_class::routes_t & routes
	);
	public: field_value_ptr  protoMfNodeValue(
		openvrml::browser & browser,
                 const scope_ptr & scope,
                 const node_interface_set & proto_interfaces,
                 proto_node_class::is_map_t & is_map,
                 proto_node_class::routes_t & routes
	);
	public: bool  boolValue();
	public: void colorValue(
		color & c
	);
	public: float  colorComponent();
	public: float  floatValue();
	public: int32  intValue();
	public: void rotationValue(
		rotation & r
	);
	public: double  doubleValue();
	public: void vec2fValue(
		vec2f & v
	);
	public: void vec3fValue(
		vec3f & v
	);
private:
	static const char* tokenNames[];
#ifndef NO_STATIC_CONSTS
	static const int NUM_TOKENS = 48;
#else
	enum {
		NUM_TOKENS = 48
	};
#endif
	
	static const unsigned long _tokenSet_0_data_[];
	static const ANTLR_USE_NAMESPACE(antlr)BitSet _tokenSet_0;
	static const unsigned long _tokenSet_1_data_[];
	static const ANTLR_USE_NAMESPACE(antlr)BitSet _tokenSet_1;
	static const unsigned long _tokenSet_2_data_[];
	static const ANTLR_USE_NAMESPACE(antlr)BitSet _tokenSet_2;
	static const unsigned long _tokenSet_3_data_[];
	static const ANTLR_USE_NAMESPACE(antlr)BitSet _tokenSet_3;
	static const unsigned long _tokenSet_4_data_[];
	static const ANTLR_USE_NAMESPACE(antlr)BitSet _tokenSet_4;
	static const unsigned long _tokenSet_5_data_[];
	static const ANTLR_USE_NAMESPACE(antlr)BitSet _tokenSet_5;
	static const unsigned long _tokenSet_6_data_[];
	static const ANTLR_USE_NAMESPACE(antlr)BitSet _tokenSet_6;
	static const unsigned long _tokenSet_7_data_[];
	static const ANTLR_USE_NAMESPACE(antlr)BitSet _tokenSet_7;
};

#endif /*INC_Vrml97Parser_hpp_*/
