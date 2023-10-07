#include "lexer.h"
#include "common.h"
#include "tokens.h"
#include "types.h"
#include "visitor.h"
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include "vendor/klib/khash.h"

extern char *optarg;
extern int optind;
extern int optopt;
extern int opterr;
extern int optreset;

// Main reference: https://en.wikipedia.org/wiki/Tail_recursive_parser

// Eventually you need to make lexing and parsing concurrent... which is fine because you have just one lookahead.
// Dear god will do typedefs later
// See https://stackoverflow.com/questions/59482460/how-to-handle-ambiguity-in-syntax-like-in-c-in-a-parsing-expression-grammar-l#:~:text=The%20well%2Dknown%20%22typedef%20problem,to%20the%20lexer%20during%20parsing.

// [x86_64ABI Figure 3.1] https://refspecs.linuxbase.org/elf/x86_64-abi-0.98.pdf
// TODO: Move this to the visitor, i.e. get_primitive_type_size
const MachineSizes X86_64_SIZES = {
  .char_size = 1,
  .short_size = 2,
  .int_size = 4,
  .long_size = 8,
  .long_long_size = 8,
  .pointer_size = 8,
  .float_size = 4,
  .double_size = 8,
  .long_double_size = 16,
};

typedef struct {
  const Type *type;
  void *value;
} Symbol;

struct SymbolTable;
KHASH_MAP_INIT_INT(SymbolTableMap, Symbol *)
typedef struct SymbolTable {
  kh_SymbolTableMap_t *map;
  struct SymbolTable *parent;
} SymbolTable;

typedef struct DeclarationSpecifiers {
  Type *base_type;
  // TODO: deal with these later... some may be folded into Type.
  StorageClassSpecifier storage_class;
  TypeQualifiers type_qualifiers;
} DeclarationSpecifiers;

// Type *max_type(const Type *t1, const Type *t2);
// int compare_type(const Type *t2, const Type *t2);

typedef enum {
  DC_ERROR = 0,
  DC_SCALAR,
  DC_ARRAY,
  DC_FUNCTION,
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

SymbolTable *new_symbol_table() {
  SymbolTable *ret = checked_calloc(1, sizeof(SymbolTable));
  ret->map = kh_init_SymbolTableMap();
  return ret;
}

void *get_symbol(const struct SymbolTable *tab, int string_id) {
  if (!tab)
    return 0;
    
  khiter_t map_iter = kh_get_SymbolTableMap(tab->map, string_id);
  if (map_iter == kh_end(tab->map))  // not found; to go parent
    return get_symbol(tab->parent, string_id);

  return kh_val(tab->map, map_iter);
}

void print_json_kv_str(FILE *out, const char *key, const char *value) {
  fprintf(out, "\"%s\":\"%s\",", key, value);
}
#define print_json_kv(out, key, format, value) fprintf(out, "\"%s\":" format ",", key, value)

void type_to_json_recur(FILE *out, const Type *type) {
  fputc('{', out);
  print_json_kv_str(out, "_type", "Type");
  switch (type->kind) {
    case TY_VOID:
      print_json_kv_str(out, "kind", "TY_VOID");
      break;
    case TY_INTEGER:
      print_json_kv_str(out, "kind", "TY_INTEGER");
      print_json_kv(out, "size", "%d", type->size);
      print_json_kv(out, "is_unsigned", "%d", type->is_unsigned);
      break;
    case TY_FLOAT:
      print_json_kv_str(out, "kind", "TY_FLOAT");
      print_json_kv(out, "size", "%d", type->size);
      break;
    default:
      THROWF(EXC_INTERNAL, "type kind %d not supported", type->kind)
  }
  fputc('}', out);
}

char *type_to_json(const Type *type) {
  char *buf;
  size_t size;
  FILE *out = checked_open_memstream(&buf, &size);
  type_to_json_recur(out, type);
  checked_fclose(out);
  return buf;
}

typedef struct {
  ScannerCont *scont;  
  // int depth;
  int pos;  // current token
  MachineSizes sizes;
  MachineSizes aligns;
  Visitor *visitor;
  SymbolTable *symtab;
  StringPool *string_pool;
  DECLARE_VECTOR(Token, tokens)
} ParserCont;

typedef struct {
  int gen_lvalue;  // else gen rvalue
  int gen_jump;  // else gen rvalue
  int gen_constexpr;  // ???
} ParseControl;

ParserCont make_parser_cont(ScannerCont *scont, Visitor *visitor) {
  ParserCont ret = {
    .scont = scont,
    .sizes = X86_64_SIZES,
    .aligns = X86_64_SIZES,
    .visitor = visitor,
    .symtab = new_symbol_table(),
  };
  NEW_VECTOR(ret.tokens, sizeof(Token));
  return ret;
}

SymbolTable *push_new_symbol_table(ParserCont *cont) {
  SymbolTable *ret = new_symbol_table();
  ret->parent = cont->symtab;
  cont->symtab = ret;
  DEBUG_PRINT_EXPR(
    "pushing new symbol table = %p; old symbol table was %p",
    (void *) cont->symtab,
    (void *) cont->symtab->parent
  );
  return ret;
}

void pop_symbol_table(ParserCont *cont) {
  SymbolTable *top_tab = cont->symtab;
  // can't free just yet...
  //
  // Symbol *x;
  // kh_foreach_value(top_tab->map, x, free(x))
  // kh_destroy_SymbolTableMap(top_tab->map);
  cont->symtab = top_tab->parent;
  // free(top_tab);
}

void insert_symbol(SymbolTable *tab, int ident_string_id, const Type *type, void *value) {
  khiter_t map_iter = kh_get_SymbolTableMap(tab->map, ident_string_id);
  THROWF_IF(map_iter != kh_end(tab->map), EXC_PARSE_SYNTAX, "symbol ident_string_id %d is already defined in current scope", ident_string_id);
  int ret;
  map_iter = kh_put_SymbolTableMap(tab->map, ident_string_id, &ret);
  THROW_IF(ret == -1, EXC_SYSTEM, "kh_put failed");

  kh_val(tab->map, map_iter) = value;

  // debug
  khiter_t get_iter = kh_get_SymbolTableMap(tab->map, ident_string_id);
  THROWF_IF(
    get_iter == kh_end(tab->map),
    EXC_INTERNAL,
    "could not get ident_string_id %d back from the symbol map hashtable", ident_string_id
  );
  Symbol *get_value = kh_val(tab->map, get_iter);
  assert(value == get_value);
}

// Helpers
// #define get_string(cont, id) cont->scont->string_pool->string_id[id]
// these are functions instead of macros so we can call them in the debugger
Token peek(ParserCont *cont) {
  return (cont)->tokens[(cont)->pos];
}

const char *peek_str(ParserCont *cont) {
  Token tok = peek(cont);
  switch (tok.kind) {
    case TOK_IDENT:
      return fmtstr("%s[%s]", TOKEN_NAMES[tok.kind], tok.identifier_name);
    case TOK_INTEGER_LITERAL:
      return fmtstr("%s[%d]", TOKEN_NAMES[tok.kind], tok.int64_val);
    default:
      return TOKEN_NAMES[peek(cont).kind];
  }
}

void backtrack_to(ParserCont *cont, int new_pos) {
  cont->pos = new_pos;
}

#define consume(cont) do { \
  THROW_IF(cont->pos == cont->tokens_size, EXC_PARSE_SYNTAX, "EOF reached without finishing parse"); \
  DEBUG_PRINT_EXPR("consumed %s", peek_str(cont)); \
  cont->pos++; \
} while (0)

#define EXPECT(cont, tok_kind) \
  THROWF_IF( \
    peek(cont).kind != (tok_kind), \
    EXC_PARSE_SYNTAX, \
    "Expected %s but saw %s", \
    TOKEN_NAMES[tok_kind], \
    TOKEN_NAMES[peek(cont).kind] \
  )

// #define is_parser_eof(cont) (cont)->pos == (cont)->tokens_size;

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

// parse_primary_expr needs this forward declaration
void *parse_expr(ParserCont *cont, ParseControl *ctl);

void *parse_primary_expr(ParserCont *cont, ParseControl *ctl) {
  PRINT_ENTRY();
  Token tok = peek(cont);
  void *ret;
  switch (tok.kind) {
    case TOK_IDENT:
      consume(cont);
      ret = get_symbol(cont->symtab, tok.string_id);
      THROWF_IF(!ret,
        EXC_PARSE_SYNTAX,
        "identifier %s with string id %d not found in symtab %p",
        tok.identifier_name, tok.string_id, (void *) cont->symtab
      );
      return ret;
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

#define scalar_type_specifier_list TOK_void, TOK_char, TOK_short, TOK_int, TOK_long, TOK_float, TOK_double, \
  TOK_signed, TOK_unsigned

#define is_type_specifier_first(op) (tok_is_in(op, scalar_type_specifier_list, TOK_struct, TOK_union, TOK_enum))
#define is_declaration_specifier_first(op) is_type_specifier_first(op)

// int is_typedef_name(ParserCont *cont, int string_id) {
//   return 0;
// }

DeclarationSpecifiers parse_declaration_specifiers(ParserCont *cont) {
  PRINT_ENTRY();

  // Can be any of the basic types, 0 = ERROR if not seen, or TOK_IDENT to specify typedef
  TokenKind parsed_type_tok = TOK_ERROR;
  int parsed_type_specifier = 0;
  int parsed_unsigned = 0;  // 0 for default, -1 for seen signed, +1 for seen unsigned. unsigned <=> parsed_sign == 1
  int parsed_long_short = 0; // 0 for default, -1 for short, > 0 for long

  for (;;) {
    Token tok = peek(cont);
    switch (tok.kind) {
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
        break;
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
      case TOK_struct: case TOK_union: case TOK_enum: 
        THROW(EXC_INTERNAL, "struct, union, enum not supported");
      // case TOK_IDENT:
      //   if (is_typedef_name(cont, tok.string_id)) {
      //     THROWF(EXC_PARSE_SYNTAX, "Expected typedef but identifier %d was not", tok.string_id);
      //   }
      //   goto label_finish;
      default:
        THROW_IF(!parsed_type_specifier, EXC_PARSE_SYNTAX, "didn't parse any type specifier");
        goto label_finish;
    }
  }
label_finish:
  // If parsed_type_tok is sitll the default value (TOK_ERROR), then the type is an int
  if (parsed_type_tok == TOK_ERROR) {
    parsed_type_tok = TOK_int;
  }
  int is_unsigned = parsed_unsigned == 1;
  int int_size = -1, int_align = -1;
  switch (parsed_long_short) {
    case -1: int_size = cont->sizes.short_size; int_align = cont->aligns.short_size; break;
    case 0: int_size = cont->sizes.int_size; int_align = cont->aligns.int_size; break;
    case 1: int_size = cont->sizes.long_size; int_align = cont->aligns.long_size; break;
    case 2: int_size = cont->sizes.long_long_size;  int_align = cont->aligns.long_long_size;break;
    default: assert(0 && "Unreachable!");
  }
  if (parsed_type_tok == TOK_char) {
    int_size = cont->sizes.char_size; int_align = cont->aligns.char_size;
  }
  assert(int_size != -1 && "Oops");
  assert(int_align != -1 && "Oops");

  Type *ty = checked_calloc(1, sizeof(Type));
  switch (parsed_type_tok) {
    case TOK_void: ty->kind = TY_VOID; break;
    case TOK_char: case TOK_int: ty->kind = TY_INTEGER; ty->size = int_size; ty->align = int_align; ty->is_unsigned = is_unsigned; break;
    case TOK_float: ty->kind = TY_FLOAT; ty->size = cont->sizes.float_size; ty->align = cont->aligns.float_size; break;
    case TOK_double:
      ty->kind = TY_FLOAT;
      ty->size = (parsed_long_short == 1 ? cont->sizes.long_double_size : cont->sizes.double_size);
      ty->align = (parsed_long_short == 1 ? cont->aligns.long_double_size : cont->aligns.double_size);
      break;
    default: assert(0 && "Unreachable!");
  }
  return (DeclarationSpecifiers) {.base_type = ty};
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
    ret->ident = ident.identifier_name;
    consume(cont);
  }

  if (peek(cont).kind == TOK_LEFT_PAREN) {
    // parse function declarator
    ret->kind = DC_FUNCTION;
    consume(cont);

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
  } else if (peek(cont).kind == TOK_LEFT_BRACKET) {
    parse_array_declarator_rest(cont, ret);
  }
  return ret;
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

Type *new_variable_array_type(Type *child_type, Declarator *declarator) {
  assert(0 && "Unimplemented!");
}

Type *new_array_type(Type *base_type, Declarator *declarator) {
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

Type *new_type_from_declaration(DeclarationSpecifiers decl_specs, Declarator *declarator) {
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
  insert_symbol(cont->symtab, declarator->ident_string_id, type, declaration);
  fprintf(stderr, "DEBUG: Declared variable %s with string id %d in symtab %p\n",
    declarator->ident, declarator->ident_string_id, (void *) cont->symtab);
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
  pop_symbol_table(cont);
}

void parse_compound_statement(ParserCont *cont) {
  PRINT_ENTRY()
  THROW_IF(peek(cont).kind != TOK_LEFT_BRACE, EXC_PARSE_SYNTAX, "expected {");
  consume(cont);
  push_new_symbol_table(cont);
  parse_compound_statement_rest(cont);
}

void parse_function_definition_rest(ParserCont *cont, DeclarationSpecifiers decl_specs, Declarator *func_declarator) {
  PRINT_ENTRY();
  assert(func_declarator->kind == DC_FUNCTION);
  CALL(cont->visitor, visit_function_definition_start, func_declarator->ident);
  push_new_symbol_table(cont);
  for (int i = 0; i < func_declarator->n_params; i++) {
    DeclarationSpecifiers param_decl_specs = func_declarator->param_decl_specs[i];
    Declarator *param_declarator = func_declarator->param_declarators[i];
    Type *param_type = new_type_from_declaration(param_decl_specs, param_declarator);
    void *param_value = CALL(
      cont->visitor,
      visit_function_definition_param,
      param_type,
      param_declarator->ident
    );
    insert_symbol(cont->symtab, param_declarator->ident_string_id, param_type, param_value);
    fprintf(stderr, "DEBUG: Declared function parameter %s with string id %d in symtab %p\n",
      param_declarator->ident, param_declarator->ident_string_id, (void *) cont->symtab);
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

// extern Visitor *new_ssa_visitor(FILE *out);
// extern Visitor *new_ast_visitor(FILE *out);
extern Visitor *new_x86_64_visitor(FILE *out);

static void parse_start(FILE *in, const char *filename, Visitor *visitor) {
  StringPool *pool = new_string_pool();
  ScannerCont scont = make_scanner_cont(in, filename, pool);
  ParserCont cont = make_parser_cont(&scont, visitor);
  Token tok;
  if (setjmp(global_exception_handler) == 0) {
    for (;;) {
      tok = consume_next_token(&scont);
      APPEND_VECTOR(cont.tokens, tok);
      if (tok.kind == TOK_END_OF_FILE)
        break;
    }
    parse_translation_unit(&cont);
    // void *result = parse_translation_unit(&cont);
    visitor->finalize(visitor);

    if (cont.pos != cont.tokens_size && peek(&cont).kind != TOK_END_OF_FILE) {
      fprintf(stderr, "ERROR: Parser did not reach EOF; next token is %s\n", peek_str(&cont));
    } else {
      fprintf(stderr, "Translation unit parsed completely and successfully!\n");
    }
  } else {
    PRINT_EXCEPTION();
    abort();
  }
}

void init_parser_module() {
  init_lexer_module();
}

void usage() {
  fprintf(stderr, "usage: parser_driver [options] file\n\n");
  fprintf(stderr, "options:\n");
  fprintf(stderr, "  -v <visitor> which visitor to use (choices: ssa, ast, x86_64)\n");
  fprintf(stderr, "  -o <file>    save output to this file\n");
  exit(1);
}

// hack
int main(int argc, char *argv[]) {
  FILE *out = stdout;
  VisitorConstructor visitor_ctor = 0;
  int ch;
  while ((ch = getopt(argc, argv, "v:o:")) != -1) {
    switch (ch) {
      case 'v':
        if (strcmp(optarg, "ssa") == 0) {
          // visitor_ctor = new_ssa_visitor;
          usage();
          // break;
        } else if (strcmp(optarg, "ast") == 0) {
          // visitor_ctor = new_ast_visitor;
          usage();
          // break;
        } else if (strcmp(optarg, "x86_64") == 0) {
          visitor_ctor = new_x86_64_visitor;
          break;
        }
      case 'o':
        out = checked_fopen(optarg, "w");
        break;
      case '?':
      default:
        usage();
    }
  }
  argc -= optind;
  argv += optind;
  if (argc < 1) {
    usage();
  }

  visitor_ctor = visitor_ctor ? visitor_ctor : new_x86_64_visitor;
  FILE *in = checked_fopen(argv[0], "r");
  init_parser_module();

  parse_start(in, argv[0], visitor_ctor(out));
  return 0;
}

#undef PRINT_ENTRY
