#include "lexer.h"

#include <stdarg.h>
#include "common.h"
#include "types_impl.h"
#include "visitor.h"

typedef enum {
  SC_NONE = 0,
  SC_EXTERN,
  SC_STATIC,
  // auto and register and recognized but ignored 
} StorageClass;

typedef struct DeclarationSpecifiers {
  Type *base_type;
  // TODO: deal with these later... some may be folded into Type.
  StorageClass storage_class;
  // function specifiers
  unsigned is_inline : 1;
  unsigned is_noreturn : 1;
} DeclarationSpecifiers;
#define IS_SPECIFIER_QUALIFIER_LIST(decl_specs) \
  (!(decl_specs).is_inline && !(decl_specs).is_noreturn && (decl_specs).storage_class == SC_NONE)

// Type *max_type(const Type *t1, const Type *t2);
// int compare_type(const Type *t2, const Type *t2);

typedef enum {
  DC_ERROR = 0,
  DC_SCALAR,
  DC_ARRAY,
  DC_FUNCTION,
  DC_KR_FUNCTION,
  DC_POINTER,
} DeclaratorKind;

typedef struct Declarator {
  DeclaratorKind kind;
  const char *ident;  // 0 means abstract; to pass to visitor for debug printing
  int ident_string_id;  // to put in the symbol table
  struct Declarator *child;  // for array and pointer
  union {
    struct {
      int n_params;
      // parallel arrays
      DeclarationSpecifiers *param_decl_specs;
      struct Declarator **param_declarators;
    };
    struct {
      int n_identifiers;  ///< Length of identifier_list. NOT parameters -- K&R functions have 0 parameters!
      int *identifier_ids;  ///< K&R function definitions -- NOT FULLY IMPLEMENTED
      const char **identifiers;
    };
    struct {
      int is_static;
      TypeQualifiers type_qualifiers;
      union {
        void *variable_size_expr;
        int fixed_size;
      };
    };
  };
} Declarator;

#define IS_ABSTRACT_DECLARATOR(declarator) !(declarator)->ident

typedef struct {
  ScannerCont *scont;  
  Visitor *visitor;
  Token token;
  struct {
    SymbolTable *values;
    SymbolTable *typedefs;
    SymbolTable *structs;
    SymbolTable *unions;
    SymbolTable *enums;
  } scope;
} ParserCont;

void push_scope(ParserCont *cont) {
  push_symbol_table(&cont->scope.values);
  push_symbol_table(&cont->scope.typedefs);
  push_symbol_table(&cont->scope.structs);
  push_symbol_table(&cont->scope.unions);
}

void pop_scope(ParserCont *cont) {
  pop_symbol_table(&cont->scope.values);
  pop_symbol_table(&cont->scope.typedefs);
  pop_symbol_table(&cont->scope.structs);
  pop_symbol_table(&cont->scope.unions);
}

typedef struct {
  int gen_lvalue;  // else gen rvalue
  int gen_jump;  // else gen rvalue
  int gen_constexpr;  // ???
} ParseControl;

Token peek(ParserCont *cont) {
  return cont->token;
}

const char *peek_str(ParserCont *cont) {
  Token tok = peek(cont);
  switch (tok.kind) {
    case TOK_IDENT: case TOK_STRING_LITERAL:
      return fmtstr("%s[%d:%s]", TOKEN_NAMES[tok.kind], tok.string_id, tok.string_val);
    case TOK_INTEGER_LITERAL:
      return fmtstr("%s[%d]", TOKEN_NAMES[tok.kind], tok.int64_val);
    default:
      return TOKEN_NAMES[peek(cont).kind];
  }
}

#define consume(cont) do { \
  THROW_IF(peek(cont).kind == TOK_END_OF_FILE, EXC_PARSE_SYNTAX, "EOF reached without finishing parse"); \
  DEBUG_PRINT_EXPR("consumed %s", peek_str(cont)); \
  cont->token = consume_next_token(cont->scont); \
} while (0)

ParserCont *new_parser_cont(FILE *in, const char *filename, Visitor *visitor) {
  ParserCont *ret = checked_calloc(1, sizeof(*ret));
  *ret = (ParserCont) {
    .scont = new_scanner_cont(in, filename),
    .visitor = visitor,
    .scope.values = new_symbol_table(),
    .scope.typedefs = new_symbol_table(),
    .scope.structs = new_symbol_table(),
    .scope.unions = new_symbol_table(),
  };
  consume(ret);
  return ret;
}

#define EXPECT(cont, tok_kind) \
  THROWF_IF( \
    peek(cont).kind != (tok_kind), \
    EXC_PARSE_SYNTAX, \
    "Expected %s but saw %s", \
    TOKEN_NAMES[tok_kind], \
    TOKEN_NAMES[peek(cont).kind] \
  )

#define PRINT_ENTRY() \
  fprintf(stderr, "> peeking %s at %d:%d inside %s\n", peek_str(cont), peek(cont).line_start, peek(cont).col_start, __func__);

// makes a lot of comparisons earlier
int _tok_is_in(TokenKind op, ...) {
  TokenKind other;
  va_list ap;
  va_start(ap, op);
  do {
    other = va_arg(ap, TokenKind);
    if (op == other) {
    va_end(ap);
      return 1;
    }
  } while (other != TOK_END_OF_FILE);
  va_end(ap);
  return 0;
}
#define tok_is_in(op, ...) _tok_is_in(op, __VA_ARGS__, TOK_END_OF_FILE)

#define CALL(receiver, method, ...) (receiver)->method((receiver), __VA_ARGS__)
#define CALL0(receiver, method) (receiver)->method(receiver)

void *parse_expr(ParserCont *cont, ParseControl *ctl);

void *parse_primary_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  Token tok = peek(cont);
  Value *lookup_result;
  void *ret;
  switch (tok.kind) {
    case TOK_IDENT:
      consume(cont);
      lookup_result = lookup_value(cont->scope.values, tok.string_id);
      THROWF_IF(!lookup_result,
        EXC_PARSE_SYNTAX,
        "identifier %s with string id %d not found in symtab %p",
        tok.string_val, tok.string_id, (void *) cont->scope.values
      );
      return lookup_result->value;
    case TOK_FLOAT_LITERAL:
      consume(cont);
      return cont->visitor->visit_float_literal(cont->visitor, tok.double_val);
    case TOK_INTEGER_LITERAL:
      consume(cont);
      return cont->visitor->visit_integer_literal(cont->visitor, tok.int64_val);
    case TOK_LEFT_PAREN:
      consume(cont);
      ret = parse_expr(cont, ctl);
      EXPECT(cont, TOK_RIGHT_PAREN);
      consume(cont);
      return ret;
    default:
      THROWF(EXC_PARSE_SYNTAX, "Unexpected token: %s", TOKEN_NAMES[tok.kind]);
  }
}

void *parse_function_call_rest(ParserCont *cont, void *function) {
  assert(peek(cont).kind == TOK_LEFT_PAREN);
  consume(cont);
  assert(0 && "Unimplemented");
}

// the PARSER should recursively get the array reference.
void *parse_postfix_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  void *left = parse_primary_expr(cont, ctl);
  void *right = 0;
  for (;;) {
    TokenKind op = peek(cont).kind;
    switch (op) {
      case TOK_LEFT_BRACKET:
        consume(cont);
        right = parse_expr(cont, ctl);
        left = CALL(cont->visitor, visit_array_reference, left, right, ctl->gen_lvalue);
        EXPECT(cont, TOK_RIGHT_BRACKET);
        consume(cont);
        break;
      case TOK_DOT_OP:
        consume(cont);
        EXPECT(cont, TOK_IDENT);
        const Member *member = lookup_member(cont->visitor->type_of(left), peek(cont).string_id);
        consume(cont);
        left = CALL(cont->visitor, visit_struct_reference, left, member);
        break;
      case TOK_LEFT_PAREN:
        return parse_function_call_rest(cont, left);
      default:
        return left;
    }
  }
}

void *parse_unary_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  void *ret = parse_postfix_expr(cont, ctl);
  return ret;
}

void *parse_cast_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  return parse_unary_expr(cont, ctl);
}

#define MAKE_BINOP_PARSER(parse_func, next_parse_func, continue_pred) \
void *parse_func(ParserCont *cont, ParseControl *ctl) { \
  PRINT_ENTRY(); \
  void *left = next_parse_func(cont, ctl); \
  void *right = 0; \
  for (;;) { \
    TokenKind op = peek(cont).kind; \
    if (!continue_pred(op)) \
      break; \
    consume(cont); \
    right = next_parse_func(cont, ctl); \
    left = cont->visitor->visit_binop(cont->visitor, op, left, right); \
  } \
  return left; \
}

#define multiplicative_pred(op) tok_is_in(op, TOK_STAR_OP, TOK_DIV_OP, TOK_MOD_OP)
MAKE_BINOP_PARSER(parse_multiplicative_expr, parse_cast_expr, multiplicative_pred)

#define additive_pred(op) tok_is_in(op, TOK_ADD_OP, TOK_SUB_OP)
MAKE_BINOP_PARSER(parse_additive_expr, parse_multiplicative_expr, additive_pred)

void *parse_shift_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  return parse_additive_expr(cont, ctl);
}

void *parse_relational_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  return parse_shift_expr(cont, ctl);
}

void *parse_equality_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  return parse_relational_expr(cont, ctl);
}

void *parse_and_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  return parse_equality_expr(cont, ctl);
}

void *parse_exclusive_or_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  return parse_and_expr(cont, ctl);
}

void *parse_inclusive_or_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  return parse_exclusive_or_expr(cont, ctl);
}

void *parse_logical_and_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  return parse_inclusive_or_expr(cont, ctl);
}

void *parse_logical_or_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  return parse_logical_and_expr(cont, ctl);
}

void *parse_conditional_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  return parse_logical_or_expr(cont, ctl);
}

#define is_assignment_op(op) tok_is_in( \
  op, TOK_ASSIGN_OP, TOK_MUL_ASSIGN, TOK_MOD_ASSIGN, TOK_ADD_ASSIGN, TOK_SUB_ASSIGN, TOK_LEFT_ASSIGN, \
  TOK_RIGHT_ASSIGN, TOK_AND_ASSIGN, TOK_XOR_ASSIGN, TOK_OR_ASSIGN \
)

// x = y = z = 1  <=>  x = (y = (z = 1))
void *parse_assignment_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  ctl->gen_lvalue = 1;
  void *left = parse_conditional_expr(cont, ctl);  // which could be an unary expression
  if (ctl->gen_lvalue != 1) {
    // could not generate an lvalue => not an unary expression
    return left;
  }

  TokenKind op = peek(cont).kind;
  if (!is_assignment_op(op))
    return left;

  consume(cont);
  assert(ctl->gen_lvalue);
  void *right = parse_assignment_expr(cont, ctl);
  return CALL(cont->visitor, visit_assign, TOK_ASSIGN_OP, left, right);
}

#define scalar_type_specifier_list TOK_void, TOK_char, TOK_short, TOK_int, TOK_long, TOK_float, TOK_double, \
  TOK_signed, TOK_unsigned
#define is_type_specifier_first(op) (tok_is_in(op, scalar_type_specifier_list, TOK_struct, TOK_union, TOK_enum))
#define is_declaration_specifier_first(op) is_type_specifier_first(op)

// int is_typedef_name(ParserCont *cont, int string_id) {
//   return 0;
// }

Type *parse_struct(ParserCont *cont);

DeclarationSpecifiers parse_declaration_specifiers(ParserCont *cont) {
  PRINT_ENTRY();

  // Can be any of the basic types, 0 = ERROR if not seen, or TOK_IDENT to specify typedef
  TokenKind parsed_type_tok = TOK_ERROR;
  int parsed_type_specifier = 0;
  int parsed_unsigned = 0;  // 0 for default, -1 for seen signed, +1 for seen unsigned. unsigned <=> parsed_sign == 1
  int parsed_long_short = 0; // 0 for default, -1 for short, > 0 for long
  DeclarationSpecifiers ret = {0};
  TypeQualifiers type_qualifiers;

  for (;;) {
    Token tok = peek(cont);
    switch (tok.kind) {
      case TOK_const: type_qualifiers.is_const = 1; consume(cont); continue;
      case TOK_restrict: type_qualifiers.is_restrict = 1; consume(cont); continue;
      case TOK_volatile: type_qualifiers.is_volatile = 1; consume(cont); continue;
      case TOK_inline: ret.is_inline = 1; consume(cont); continue;
      case TOK__Noreturn: ret.is_noreturn = 1; consume(cont); continue;
      case TOK_extern:
        THROW_IF(
          ret.storage_class != SC_NONE,
          EXC_PARSE_SYNTAX,
          "Storage class extern conflicts with earlier storage class"
        );
        ret.storage_class = SC_EXTERN;
        consume(cont);
        continue;
      case TOK_static:
        THROW_IF(
          ret.storage_class != SC_NONE,
          EXC_PARSE_SYNTAX,
          "Storage class extern conflicts with earlier storage class"
        );
        ret.storage_class = SC_STATIC;
        consume(cont);
        continue;
      case TOK_void: case TOK_char: case TOK_int: case TOK_float: case TOK_double:
        THROWF_IF(
          parsed_type_tok != TOK_ERROR,
          EXC_PARSE_SYNTAX,
          "type %s conflicts with earlier type %s",
          TOKEN_NAMES[tok.kind],
          TOKEN_NAMES[parsed_type_tok]
        );
        parsed_type_tok = tok.kind;
        parsed_type_specifier = 1;
        consume(cont);
        continue;
      case TOK_signed:
        THROW_IF(
          !tok_is_in(parsed_type_tok, TOK_int, TOK_char, TOK_ERROR),
          EXC_PARSE_SYNTAX,
          "only int and char can be signed"
        );
        THROW_IF(parsed_unsigned != 0, EXC_PARSE_SYNTAX, "signed conflicts with earlier unsigned");
        parsed_type_specifier = 1;
        parsed_unsigned = -1;
        consume(cont);
        continue;
      case TOK_unsigned:
        THROW_IF(
          !tok_is_in(parsed_type_tok, TOK_int, TOK_char, TOK_ERROR),
          EXC_PARSE_SYNTAX,
          "only int and char can be unsigned"
        );
        THROW_IF(parsed_unsigned != 0, EXC_PARSE_SYNTAX, "unsigned conflicts with earlier signed");
        parsed_type_specifier = 1;
        parsed_unsigned = 1;
        consume(cont);
        continue;
      case TOK_short:
        THROW_IF(
          !tok_is_in(parsed_type_tok, TOK_int, TOK_ERROR),
          EXC_PARSE_SYNTAX,
          "only int can be short"
        );
        THROW_IF(parsed_long_short != 0, EXC_PARSE_SYNTAX, "short conflicts with earlier longs");
        parsed_type_specifier = 1;
        parsed_long_short = -1;
        consume(cont);
        continue;
      case TOK_long:
        THROW_IF(
          !tok_is_in(parsed_type_tok, TOK_int, TOK_double, TOK_ERROR),
          EXC_PARSE_SYNTAX,
          "only int or double can be long"
        );
        THROW_IF(parsed_long_short < 0, EXC_PARSE_SYNTAX, "long conflicts with earlier shorts");
        THROW_IF(parsed_long_short == 2, EXC_PARSE_SYNTAX, "can have at most 2 longs");
        THROW_IF(
          (parsed_type_tok == TOK_double && parsed_long_short == 1),
          EXC_PARSE_SYNTAX, "double can have at most 1 long"
        );
        parsed_type_specifier = 1;
        parsed_long_short++;
        consume(cont);
        continue;
      case TOK_struct: case TOK_union:
        THROW_IF(
          parsed_type_specifier || parsed_unsigned || parsed_long_short,
          EXC_PARSE_SYNTAX,
          "Cannot combine primitive type specifier, unsigned/unsigned, or long/short with struct or union"
        );
        ret.base_type = parse_struct(cont);
        THROW_IF(!ret.base_type, EXC_INTERNAL, "Forward declaration not supported yet.");
        parsed_type_specifier = 1;
        continue;
      case TOK_enum:
        THROW(EXC_INTERNAL, "enum not supported");
      default:
        THROW_IF(!parsed_type_specifier, EXC_PARSE_SYNTAX, "didn't parse any type specifier");
        goto label_finish;
    }
  }
label_finish:
  if (!ret.base_type) {
    // Create a numeric type of the correct type and signedness. If we parsed an aggregate type, type would not be null
    // and we wouldn't hit this branch.
    //
    // If parsed_type_tok is still the default value (TOK_ERROR), then the type is an int
    if (parsed_type_tok == TOK_ERROR) {
      parsed_type_tok = TOK_int;
    }
    int is_unsigned = parsed_unsigned == 1;
    Type *primitive_type;
    Visitor *v = cont->visitor;

    switch (parsed_type_tok) {
      case TOK_void: primitive_type = &VOID_TYPE; break;
      case TOK_char: primitive_type = is_unsigned ? &v->unsigned_char_type : &v->char_type; break;
      case TOK_int:
        switch (parsed_long_short) {
          case -1: primitive_type = is_unsigned ? &v->unsigned_short_type : &v->short_type; break;
          case 0:  primitive_type = is_unsigned ? &v->unsigned_int_type : &v->int_type; break;
          case 1:  primitive_type = is_unsigned ? &v->unsigned_long_type : &v->long_type; break;
          case 2:  primitive_type = is_unsigned ? &v->unsigned_long_long_type : &v->long_long_type; break;
        }
        break;
      case TOK_float: primitive_type = &v->float_type;
      case TOK_double: primitive_type = parsed_long_short == 1 ? &v->long_double_type : &v->double_type; break;
      default: assert(0 && "Unreachable!");
    }

    // if this is a qualified type, we need to copy
    if (IS_QUALIFIED(type_qualifiers)) {
      ret.base_type = checked_malloc(sizeof(Type));
      checked_memcpy(ret.base_type, primitive_type, sizeof(Type));
      ret.base_type->qualifiers = type_qualifiers;
    } else {
      ret.base_type = primitive_type;
    }
  } else {
    // TODO: check if type already exists and if we are redefining it...
    // TODO: make base_type const
    ret.base_type->qualifiers = type_qualifiers;
  }
  return ret;
}

// nesting function declarators not supported yet
#define is_array_declarator_first(op) (tok_is_in(op, TOK_LEFT_BRACKET))
void parse_array_declarator_rest(ParserCont *cont, Declarator *declarator) {
  PRINT_ENTRY();
  // only arrays of arrays; don't be declaring arrays of function pointers now
  assert(peek(cont).kind == TOK_LEFT_BRACKET);
  consume(cont);
  declarator->kind = DC_ARRAY;

  THROW_IF(peek(cont).kind != TOK_INTEGER_LITERAL, EXC_INTERNAL, "Only fixed size arrays supported for now.");
  Token size_tok = peek(cont);
  THROW_IF(size_tok.int64_val < 0, EXC_PARSE_SYNTAX, "array size must be nonnegative.");
  consume(cont);

  declarator->fixed_size = (int) size_tok.int64_val;
  EXPECT(cont, TOK_RIGHT_BRACKET);
  consume(cont);

  if (is_array_declarator_first(peek(cont).kind)) {
    declarator->child = checked_calloc(1, sizeof(Declarator)); 
    parse_array_declarator_rest(cont, declarator->child);
  }
}

#define is_declarator_or_abstract_declarator_first(op) tok_is_in(op, TOK_STAR_OP, TOK_LEFT_PAREN, TOK_IDENT)
Declarator *parse_declarator_or_abstract_declarator(ParserCont *cont) {
  PRINT_ENTRY();
  Declarator *ret = calloc(1, sizeof(Declarator));

  if (peek(cont).kind == TOK_STAR_OP) {
    consume(cont);
    ret->kind = DC_POINTER;
    ret->child = parse_declarator_or_abstract_declarator(cont);
    return ret;
  }
  if (peek(cont).kind == TOK_LEFT_PAREN) {
    consume(cont);
    ret = parse_declarator_or_abstract_declarator(cont);
    EXPECT(cont, TOK_RIGHT_PAREN);
    consume(cont);
    return ret;
  }
  ret->kind = DC_SCALAR;  // scalar until proven otherwise

  if (peek(cont).kind == TOK_IDENT) {
    Token ident = peek(cont);
    ret->ident_string_id = ident.string_id;
    ret->ident = ident.string_val;
    consume(cont);
  }

  if (peek(cont).kind == TOK_LEFT_BRACKET) {
    parse_array_declarator_rest(cont, ret);
    return ret;
  }
  if (peek(cont).kind == TOK_LEFT_PAREN) {
    // parse function declarator
    ret->kind = DC_KR_FUNCTION;
    consume(cont);

    if (peek(cont).kind == TOK_RIGHT_PAREN) {
      // special case -- no parameters. n_params was already set to 0.
      consume(cont);
      return ret;
    }
    if (peek(cont).kind == TOK_void) {
      // the other special case with no parameters
      consume(cont);
      EXPECT(cont, TOK_RIGHT_PAREN);
      consume(cont);
      return ret;
    }
    if (peek(cont).kind == TOK_IDENT) {
      DECLARE_VECTOR(int, identifier_ids)
      DECLARE_VECTOR(const char *, identifiers)
      NEW_VECTOR(identifier_ids, sizeof(*identifier_ids));
      NEW_VECTOR(identifiers, sizeof(*identifiers));
      for (;;) {
        APPEND_VECTOR(identifier_ids, peek(cont).string_id);
        APPEND_VECTOR(identifiers, peek(cont).string_val);
        consume(cont);

        if (peek(cont).kind == TOK_RIGHT_PAREN)
          break;

        EXPECT(cont, TOK_COMMA);
        consume(cont);
      }
      ret->n_identifiers = identifier_ids_size;
      ret->identifier_ids = identifier_ids;
      ret->identifiers = identifiers;
      return ret;
    }

    // it's not a K&R function
    ret->kind = DC_FUNCTION;
    DECLARE_VECTOR(DeclarationSpecifiers, param_decl_specs)
    DECLARE_VECTOR(Declarator *, param_declarators)
    NEW_VECTOR(param_decl_specs, sizeof(DeclarationSpecifiers));
    NEW_VECTOR(param_declarators, sizeof(Declarator *));
  
    for (;;) {
      DeclarationSpecifiers curr_decl_specs = parse_declaration_specifiers(cont);
      Declarator *curr_declarator = parse_declarator_or_abstract_declarator(cont);
      APPEND_VECTOR(param_decl_specs, curr_decl_specs);
      APPEND_VECTOR(param_declarators, curr_declarator);

      if (peek(cont).kind != TOK_COMMA)
        break;

      consume(cont);
    }
    assert(VECTOR_SIZE(param_decl_specs) == VECTOR_SIZE(param_declarators));
    EXPECT(cont, TOK_RIGHT_PAREN);
    consume(cont);
    ret->n_params = VECTOR_SIZE(param_decl_specs);
    ret->param_decl_specs = param_decl_specs;
    ret->param_declarators = param_declarators;
    return ret;
  }
  return ret;
}

/** Increment *p_offset (potentially by 0) to be divisible by align. */
void align_to(int *p_offset, int align) {
  int rem = *p_offset % align;
  *p_offset += (rem == 0) ? 0 : align - rem;
  assert(*p_offset % align == 0);
}

typedef struct StructBuilder StructBuilder;
typedef void (Appender)(StructBuilder *builder, const Type *type, const char *ident, int ident_id);

/** Collect the temporary variables needed to build a struct type. */
typedef struct StructBuilder {
  Appender *append;
  int offset;  ///< UNALIGNED offset for the next member. Call align_to before using.
  int align;  ///< The alignment of a struct or union is the max alignment of its members
  void *member_map;  ///< Map from string identifier IDs to Member objects.
  DECLARE_VECTOR(const Member *, members)  ///< Also store member declarations as a vector, to support anonymous structures and unions.
} StructBuilder;

/** Create a new StructBuilder with parent SymbolTable given by parent (which could be NULL). */
StructBuilder make_struct_builder(Appender *append) {
  StructBuilder ret = {
    .append = append,
    .member_map = new_member_map(),
  };
  NEW_VECTOR(ret.members, sizeof(*ret.members));
  return ret;
}

void union_append_member(StructBuilder *builder, const Type *type, const char *ident, int ident_id) {
  builder->align = MAX(builder->align, type->align);
  Member *member = new_member(type, builder->offset, ident, ident_id);
  fprintf(stderr, "DEBUG %s:%d: inserting member %s of type %d at offset %d in %p\n", __func__, __LINE__, ident, type->kind, builder->offset, builder->member_map);
  insert_member(builder->member_map, member);
  APPEND_VECTOR(builder->members, member);
}

void struct_append_member(StructBuilder *builder, const Type *type, const char *ident, int ident_id) {
  // A struct is a just a union where the offsets are not all 0
  align_to(&builder->offset, type->align);
  union_append_member(builder, type, ident, ident_id);
  builder->offset += type->size;
}

Type *new_type_from_declaration(DeclarationSpecifiers decl_specs, const Declarator *declarator);

/** 
 * Parse declarator list for either struct or union, depending on the append parameter.
 * Returns nothing. Stores the results in the builder for the caller to manipulate.
 */
void parse_struct_declarator_list(
  ParserCont *cont,
  StructBuilder *builder,
  DeclarationSpecifiers decl_specs
) {
  for (;;) {
    Declarator *declarator = parse_declarator_or_abstract_declarator(cont);
    THROW_IF(IS_ABSTRACT_DECLARATOR(declarator), EXC_PARSE_SYNTAX, "declarator in struct or union must have name");
    Type *type = new_type_from_declaration(decl_specs, declarator);

    builder->append(builder, type, declarator->ident, declarator->ident_string_id);
    if (peek(cont).kind != TOK_COMMA)
      break;

    consume(cont);
  }
}

Type *parse_struct_declaration_list(ParserCont *cont, StructBuilder *builder) {
  EXPECT(cont, TOK_LEFT_BRACE);
  consume(cont);
  for (;;) {
    if (peek(cont).kind == TOK_RIGHT_BRACE) {
      consume(cont);
      break;
    }
    DeclarationSpecifiers specifier_qualifier_list = parse_declaration_specifiers(cont);
    THROW_IF(
      !IS_SPECIFIER_QUALIFIER_LIST(specifier_qualifier_list),
      EXC_PARSE_SYNTAX,
      "only type specifiers and type qualifiers allowed inside a struct declaration list"
    );

    if (peek(cont).kind == TOK_SEMI) {
      // No declarator => anonymous struct or union => left type must be type or union
      const Type *child_type = specifier_qualifier_list.base_type;
      THROW_IF(
        child_type->kind != TY_STRUCT && child_type->kind != TY_UNION,
        EXC_PARSE_SYNTAX,
        "Except for anonymous structs and unions, member declarations must have a declarator list"
      );
      consume(cont);

      /* 6.7.2.1.13: "The members of an anonymous structure or union are considered to be members of the containing
       * structure or union."
       * The appender depends on the child type: if the child is a struct and the parent is a union, then we need to
       * append the members as a struct, i.e.
       *
       *   union { struct {int s; int t}, int u };
       *
       * needs to have s at offset 0 and t at offset 4 (and u at offset 0 again). Vice versa if the child is a union
       * and the parent is a struct.
       */
      Appender *child_append = child_type->kind == TY_STRUCT ? struct_append_member : union_append_member;
      for (int i = 0; i < child_type->n_members; i++) {
        const Member *m = child_type->members[i];
        child_append(builder, m->type, m->ident, m->_ident_id);
      }
      // If the parent is a union, we need to reset offset to 0 in case the child was a struct, because the previous
      // child_append call has incremented offset.
      if (builder->append == union_append_member) {
        builder->offset = 0;
      }
    } else {
      parse_struct_declarator_list(cont, builder, specifier_qualifier_list);
      EXPECT(cont, TOK_SEMI);
      consume(cont);
    }
  }
  // Now we can make a new struct or union type.
  if (builder->offset == 0) assert(builder->members_size == 0);

  // One last adjustment to alignment
  align_to(&builder->offset, builder->align);
  Type *this_type = checked_calloc(1, sizeof(*this_type));
  *this_type = (Type) {
    .size = builder->offset,
    .align = builder->align,
    .member_map = builder->member_map,
    .n_members = builder->members_size,
    .members = builder->members,
  };
  return this_type;
}

Type *parse_struct(ParserCont *cont) {
  TokenKind op = peek(cont).kind;
  assert(tok_is_in(op, TOK_struct, TOK_union));
  consume(cont);

  Token tag = {0};
  if (peek(cont).kind == TOK_IDENT) {
    tag = peek(cont);
    consume(cont);
  }

  Type *this_type = 0;
  TypeKind type_kind = op == TOK_struct ? TY_STRUCT : TY_UNION;
  if (peek(cont).kind == TOK_LEFT_BRACE) {
    Appender *append = op == TOK_struct ? struct_append_member : union_append_member;
    StructBuilder builder = make_struct_builder(append);
    this_type = parse_struct_declaration_list(cont, &builder);
    this_type->kind = type_kind;
    this_type->tag = tag.string_id;
  }

  if (!this_type && !tag.string_id)
    THROW(EXC_PARSE_SYNTAX, "struct or union declaration must declare a tag or a type specifier.");
  // now at least one of this_type or tag.string_id is true
  if (!tag.string_id)
    return this_type;

  // If we did not declare a struct specifier, create an incomplete type.
  if (!this_type) {
    this_type = checked_calloc(1, sizeof(*this_type));
    *this_type = (Type) {
      .kind = type_kind,
      .tag = tag.string_id
    };
  }

  assert(tag.string_id > 0 && this_type && this_type->kind == type_kind && this_type->tag == tag.string_id);

  // Check whether this tag has already been defined IN THE SAME SCOPE. If the previous definition is incomplete,
  // replace the value with this_type. Otherwise, check whether this_type is compatible with the previous
  // definition and raise an exception if incompatible. Finally, if this tag is not yet defined, then insert it.

  // Since we limit our lookup to the SAME SCOPE, we cannot use lookup_symbol, which will traverse parent scopes.
  // Use the hashmap functions directly.
  SymbolTable *tab = op == TOK_struct ? cont->scope.structs : cont->scope.unions;
  Type *existing_type = lookup_type_norecur(tab, tag.string_id);
  if (!existing_type) { // not found
    insert_symbol(tab, tag.string_id, this_type);
    return this_type;
  }
  // found
  assert(existing_type && existing_type->kind == this_type->kind);

  Type *composite_type = get_composite_type(existing_type, this_type);
  THROWF_IF(
    !composite_type,
    EXC_PARSE_SYNTAX,
    "tag %s was already declared with an incompatible type",
    tag.string_val
  );

  // Return the existing type, to avoid polluting copies. TODO: Free...
  return composite_type;
}
      
int parse_array_designator(ParserCont *cont) {
  PRINT_ENTRY();
  EXPECT(cont, TOK_LEFT_BRACKET);
  consume(cont);
  // only constants, not constexpr, supported now
  EXPECT(cont, TOK_INTEGER_LITERAL);
  int ret = peek(cont).int64_val;
  consume(cont);
  EXPECT(cont, TOK_RIGHT_BRACKET);
  consume(cont);
  return ret;
}

void parse_array_designation(ParserCont *cont, const Type **p_type, int *p_offset) {
  PRINT_ENTRY();
  // first designator only changes offset, not type
  int index = parse_array_designator(cont);
  int child_size = cont->visitor->total_size((*p_type)->child_type);
  *p_offset += index * child_size;

  while (peek(cont).kind == TOK_LEFT_BRACKET) {
    *p_type = (*p_type)->child_type;
    child_size = cont->visitor->total_size((*p_type)->child_type);
    index = parse_array_designator(cont);
    *p_offset += index * child_size;
  }
}

void parse_initializer_list(ParserCont *cont, void *base_object, const Type *type, int offset) {
  PRINT_ENTRY();
  THROW_IF(!type->child_type, EXC_PARSE_SYNTAX, "Initializer list has more nesting levels than base type.");
  EXPECT(cont, TOK_LEFT_BRACE);
  consume(cont);

  const Type *element_type = type;
  // NONE OF THIS SUPPORTS STRUCTS UGH
  while (!IS_SCALAR_TYPE(element_type)) {
    element_type = element_type->child_type;
  }
  int element_size = element_type->size;

  const Type *next_type = 0;
  int next_offset = offset;
  for (;;) {
    TokenKind op = peek(cont).kind;
    switch (op) {
      case TOK_RIGHT_BRACE:  // trailing comma or empty
        goto label_finish;
      case TOK_LEFT_BRACKET:
        next_type = type;
        next_offset = offset;
        // A a new array designator resets the offset to the beginning of this subobject.
        parse_array_designation(cont, &next_type, &next_offset);
        EXPECT(cont, TOK_ASSIGN_OP);
        consume(cont);
        break;
      case TOK_DOT_OP:
        assert(0 && "Unimplemented!");
        break;
      default:
        break;
    }

    if (peek(cont).kind == TOK_LEFT_BRACE) {
      assert(next_type && next_offset >= 0);
      parse_initializer_list(cont, base_object, next_type->child_type, next_offset);
      /* 6.7.9.20: "If there are fewer initializers, in a brace-enclosed list than there are elements [...] the 
        remainder of the aggregate shall be initialized implicitly the same as objects that have static storage 
        duration
      */
      next_offset = offset + cont->visitor->total_size(next_type->child_type);
    } else {
      if (peek(cont).kind == TOK_INTEGER_LITERAL) {
        fprintf(stderr, "DEBUG: initializer list setting offset %d to %lld\n", offset, peek(cont).int64_val);
      } else {
        fprintf(stderr, "DEBUG: initializer list setting offset %d to some expression", offset);
      }
      ParseControl ctl = {0};
      void *right = parse_assignment_expr(cont, &ctl);
      CALL(cont->visitor, visit_assign_offset, base_object, next_offset, right);
      next_offset += element_size;
    }

    if (peek(cont).kind != TOK_COMMA)
      break;

    consume(cont);
  }
label_finish:
  EXPECT(cont, TOK_RIGHT_BRACE);
  consume(cont);
}

void parse_initializer(ParserCont *cont, void *left) {
  PRINT_ENTRY();
  if (peek(cont).kind == TOK_LEFT_BRACE) {
    CALL(cont->visitor, visit_zero_object, left);
    const Type *base_type = cont->visitor->type_of(left);
    parse_initializer_list(cont, left, base_type, 0);
    return;
  }
  ParseControl ctl = {0};
  void *right = parse_assignment_expr(cont, &ctl);
  CALL(cont->visitor, visit_assign, TOK_ASSIGN_OP, left, right);
}

Type *new_variable_array_type(Type *child_type, const Declarator *declarator) {
  assert(0 && "Unimplemented!");
}

Type *new_array_type(Type *base_type, const Declarator *declarator) {
  assert(declarator->kind == DC_ARRAY);

  Type *ret = checked_calloc(1, sizeof(Type));
  ret->kind = TY_ARRAY;
  ret->size = declarator->fixed_size;
  if (declarator->child) {
    // recursive case
    ret->child_type = new_array_type(base_type, declarator->child);
  } else {
    // base case
    ret->child_type = base_type;  // from declaration specifiers
  }
  return ret;
}

Type *new_type_from_declaration(DeclarationSpecifiers decl_specs, const Declarator *declarator) {
  Type *ret = 0;
  switch (declarator->kind) {
    case DC_SCALAR:
      ret = checked_calloc(1, sizeof(Type));
      ret = decl_specs.base_type;
      break;
    case DC_ARRAY:
      assert(IS_SCALAR_TYPE(decl_specs.base_type) && !decl_specs.base_type->child_type);
      ret = new_array_type(decl_specs.base_type, declarator);
      break;
    default:
      THROWF(EXC_INTERNAL, "Unsupported declarator kind %d", declarator->kind);
  }
  return ret;
}

void *finish_declaration(ParserCont *cont, DeclarationSpecifiers decl_specs, Declarator *declarator) {
  assert(!IS_ABSTRACT_DECLARATOR(declarator));

  Type *type = new_type_from_declaration(decl_specs, declarator);
  void *declaration = CALL(cont->visitor, visit_declaration, type, declarator->ident);
  Value *value = new_value(type, declaration);
  insert_symbol(cont->scope.values, declarator->ident_string_id, value);
  fprintf(stderr, "DEBUG: Declared variable %s with string id %d in symtab %p\n",
    declarator->ident, declarator->ident_string_id, (void *) cont->scope.values);
  return declaration;
}

void parse_declaration_rest(ParserCont *cont, DeclarationSpecifiers decl_specs, Declarator *first_declarator) {
  PRINT_ENTRY();
  THROW_IF(IS_ABSTRACT_DECLARATOR(first_declarator), EXC_PARSE_SYNTAX, "declaration must have identifier");
  Declarator *declarator = first_declarator;
  void *declaration;
  for (;;) {
    TokenKind op = peek(cont).kind;
    switch (op) {
      case TOK_SEMI:
        consume(cont);
        finish_declaration(cont, decl_specs, declarator);
        return;
      case TOK_COMMA:
        consume(cont);
        finish_declaration(cont, decl_specs, declarator);
        break;  // maybe more declarators to come
      case TOK_ASSIGN_OP:
        consume(cont);
        declaration = finish_declaration(cont, decl_specs, declarator);
        parse_initializer(cont, declaration);
        // consume comma here so we don't trigger the comma action twice
        if (peek(cont).kind == TOK_COMMA)
          consume(cont);
        break;
      default:
        THROWF(EXC_PARSE_SYNTAX, "Unexpected token %s", TOKEN_NAMES[op]);
    }
    if (peek(cont).kind == TOK_SEMI) {
      consume(cont);
      return;
    }

    // prepare loop for next declarator
    declarator = parse_declarator_or_abstract_declarator(cont);
  }
}

#define is_declaration_first(op) is_declaration_specifier_first(op)
void parse_declaration(ParserCont *cont) {
  PRINT_ENTRY();
  DeclarationSpecifiers decl_specs = parse_declaration_specifiers(cont);
  if (peek(cont).kind == TOK_SEMI) {
    // If we don't have an init declarator list, then we must have declared a tag or enum.
    THROW_IF(decl_specs.base_type->tag == 0, EXC_PARSE_SYNTAX, "Declaration with identifier must specify tag.");
    consume(cont);
    return;
  }

  // If we parsed an incomplete tagged type specification (i.e. struct S a;), we need to replace it with the complete
  // one, which was stored in the symbol table when we parsed the declaration earlier.
  Type *parsed_type = decl_specs.base_type;
  if (parsed_type->tag > 0) {
    SymbolTable *tab;
    switch (parsed_type->kind) {
      case TY_STRUCT: tab = cont->scope.structs; break;
      case TY_UNION: tab = cont->scope.unions; break;
      case TY_ENUM: tab = cont->scope.enums; break;
      default: assert(0 && "Type is invalid -- has tag but is not a tagged type.");
    }
    decl_specs.base_type = lookup_type(tab, parsed_type->tag);
  }
  Declarator *first_declarator = parse_declarator_or_abstract_declarator(cont);
  parse_declaration_rest(cont, decl_specs, first_declarator);
}


void *parse_expression_statement(ParserCont *cont) {
  PRINT_ENTRY();
  ParseControl ctl = {0};
  void *expr = parse_expr(cont, &ctl);
  EXPECT(cont, TOK_SEMI);
  consume(cont);
  return expr;
}

#define is_jump_statement_first(op) tok_is_in(op, TOK_goto, TOK_continue, TOK_break, TOK_return)
void parse_jump_statement(ParserCont *cont) {
  TokenKind op = peek(cont).kind;
  ParseControl ctl = { .gen_jump = 1 };
  void *retval = 0;
  switch (op) {
    case TOK_return:
      consume(cont);
      if (peek(cont).kind != TOK_SEMI) {
        retval = parse_expr(cont, &ctl);
      }
      THROW_IF(peek(cont).kind != TOK_SEMI, EXC_PARSE_SYNTAX, "Expected ; after return");
      consume(cont);
      CALL(cont->visitor, visit_return, retval);
      break;
    default:
      THROWF(EXC_INTERNAL, "Unimplemented jump statement %s", TOKEN_NAMES[op]);
  }
}

void parse_compound_statement(ParserCont *cont);

#define is_compound_statement_first(op) tok_is_in(op, TOK_LEFT_BRACE)
void parse_statement(ParserCont *cont) {
  PRINT_ENTRY()
  TokenKind op = peek(cont).kind;
  if (is_compound_statement_first(op)) {
    parse_compound_statement(cont);
  } else if (is_jump_statement_first(op)) {
    parse_jump_statement(cont);
  } else {
    parse_expression_statement(cont);
  }
}

void parse_block_item(ParserCont *cont) {
  PRINT_ENTRY()
  TokenKind op = peek(cont).kind;
  CALL(cont->visitor, emit_comment, "%s:%d", peek(cont).filename, peek(cont).line_start + 1);
  if (is_declaration_first(op)) {
    fprintf(stderr, "parse_block_item saw %s; parsing as declaration\n", TOKEN_NAMES[op]);
    parse_declaration(cont);
    return;
  }
  parse_statement(cont);
}

void parse_compound_statement_rest(ParserCont *cont) {
  for (;;) {
    // could have 0 items
    if (peek(cont).kind == TOK_RIGHT_BRACE) {
      consume(cont);
      break;
    }
    parse_block_item(cont);
  }
  pop_scope(cont);
}

void parse_compound_statement(ParserCont *cont) {
  PRINT_ENTRY()
  THROW_IF(peek(cont).kind != TOK_LEFT_BRACE, EXC_PARSE_SYNTAX, "expected {");
  consume(cont);
  push_scope(cont);
  parse_compound_statement_rest(cont);
}

void parse_kr_function_declaration_list(ParserCont *cont, Declarator *func_declarator) {
  assert(0 && "Unimplemented!");
  // we have to store the declarations and not just visit them in order, because the order of the identifier list
  // can differ from the order they are presented.
  int nids = func_declarator->n_identifiers;
  DeclarationSpecifiers kr_decl_sepcs[nids];
  Declarator *kr_declarators[nids];

  for (int i = 0; i < nids; i++) {
    kr_decl_sepcs[i] = parse_declaration_specifiers(cont);
    kr_declarators[i] = parse_declarator_or_abstract_declarator(cont);
    THROW_IF(IS_ABSTRACT_DECLARATOR(kr_declarators[i]), EXC_PARSE_SYNTAX, "declaration must have identifier");
  }

  // Now go through the identifiers and codegen the function
  for (int j = 0; j < func_declarator->n_identifiers; j++) {
    // linear search to find the right identifier
    int i;
    for (i = 0; i < nids; i++) {
      if (kr_declarators[i]->ident_string_id == func_declarator->identifier_ids[j])
        break;
    }
    const Type *type;
    if (i == nids) {
      type = &cont->visitor->int_type;
    } else {
      type = new_type_from_declaration(kr_decl_sepcs[i], kr_declarators[i]);
    }
    void *declaration = CALL(
      cont->visitor,
      visit_function_definition_param,
      type,
      func_declarator->identifiers[j]
    );
    Value *param_value = new_value(type, declaration);
    insert_symbol(cont->scope.values, func_declarator->identifier_ids[j],  param_value);
    // fprintf(stderr, "DEBUG: Declared function parameter %s with string id %d in symtab %p\n",
    //   param_declarator->ident, param_declarator->ident_string_id, (void *) cont->scope.values);
  }
}

void parse_function_definition_rest(ParserCont *cont, DeclarationSpecifiers decl_specs, Declarator *func_declarator) {
  PRINT_ENTRY();
  assert(func_declarator->kind == DC_FUNCTION || func_declarator->kind == DC_KR_FUNCTION);
  CALL(cont->visitor, visit_function_definition_start, func_declarator->ident);
  push_scope(cont);
  for (int i = 0; i < func_declarator->n_params; i++) {
    DeclarationSpecifiers param_decl_specs = func_declarator->param_decl_specs[i];
    Declarator *param_declarator = func_declarator->param_declarators[i];
    Type *param_type = new_type_from_declaration(param_decl_specs, param_declarator);
    void *param_declaration = CALL(
      cont->visitor,
      visit_function_definition_param,
      param_type,
      param_declarator->ident
    );
    Value *param_value = new_value(param_type, param_declaration);
    insert_symbol(cont->scope.values, param_declarator->ident_string_id, param_value);
    fprintf(stderr, "DEBUG: Declared function parameter %s with string id %d in symtab %p\n",
      param_declarator->ident, param_declarator->ident_string_id, (void *) cont->scope.values);
  }
  parse_compound_statement_rest(cont);
  CALL0(cont->visitor, visit_function_end);
  return;
}

// both external 
// An external declaration can be a declaration or a function definition. Both of them start with
//   declaration_specifiers declarator
// and then if the next token is a left brace, we know we have a function definition.
void parse_external_declaration(ParserCont *cont) {
  PRINT_ENTRY();
  DeclarationSpecifiers decl_specs = parse_declaration_specifiers(cont);
  Declarator *first_declarator = parse_declarator_or_abstract_declarator(cont);

  if (is_declaration_first(peek(cont).kind)) {
    THROW(EXC_INTERNAL, "K&R function definition with nonzero body unsupported!");
  }

  if (peek(cont).kind == TOK_LEFT_BRACE) {
    consume(cont);
    parse_function_definition_rest(cont, decl_specs, first_declarator);
  } else {
    parse_declaration_rest(cont, decl_specs, first_declarator);
  }
}

void parse_translation_unit(ParserCont *cont) {
  PRINT_ENTRY()
  parse_external_declaration(cont);
  while (peek(cont).kind != TOK_END_OF_FILE) {
    parse_external_declaration(cont);
  }
}

// Comma evaluates the first operand, discards the result, then evaluates the second operand and returns the result.
// If none of the operands had side effects, then we don't need to call any visit method. And for instruction emitters
// like SSA, the code to produce the side effects were already emitted. However, for the AST emitter, we still need to
// visit so that all of the operands get joined to the tree.
//
// So in the end this is just any other binary operator.
#define expr_pred(op) (op == TOK_COMMA)
MAKE_BINOP_PARSER(parse_expr, parse_assignment_expr, expr_pred)

void init_parser_module() {
  init_lexer_module();
}

#undef PRINT_ENTRY
