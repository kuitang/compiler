#include "lexer.h"
#include "parser.h"
#include <stdio.h>
#include "common.h"
#include "tokens.h"
#include "types.h"
#include "visitor.h"
#include <assert.h>
#include <unistd.h>
#include <stdarg.h>
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
  DeclarationSpecifiers decl_specs;
  void *value;
} Symbol;

struct SymbolTable;
KHASH_MAP_INIT_INT(SymbolTableMap, Symbol *)
typedef struct SymbolTable {
  kh_SymbolTableMap_t *map;
  struct SymbolTable *parent;
} SymbolTable;

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
  MachineSizes alignments;
  Visitor *visitor;
  SymbolTable *symtab;
  StringPool *string_pool;
  DECLARE_VECTOR(Token, tokens)
} ParserCont;

ParserCont make_parser_cont(ScannerCont *scont, Visitor *visitor) {
  ParserCont ret = {
    .scont = scont,
    .sizes = X86_64_SIZES,
    .alignments = X86_64_SIZES,
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

void insert_symbol(SymbolTable *tab, int string_id, DeclarationSpecifiers decl_specs, void *value) {
  khiter_t map_iter = kh_get_SymbolTableMap(tab->map, string_id);
  THROWF_IF(map_iter != kh_end(tab->map), EXC_PARSE_SYNTAX, "symbol string_id %d is already defined in current scope", string_id);
  int ret;
  map_iter = kh_put_SymbolTableMap(tab->map, string_id, &ret);
  THROW_IF(ret == -1, EXC_SYSTEM, "kh_put failed");

  kh_val(tab->map, map_iter) = value;

  // debug
  khiter_t get_iter = kh_get_SymbolTableMap(tab->map, string_id);
  THROWF_IF(
    get_iter == kh_end(tab->map),
    EXC_INTERNAL,
    "could not get string_id %d back from the symbol map hashtable", string_id
  );
  Symbol *get_value = kh_val(tab->map, string_id);
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
  if (tok.kind == TOK_IDENT) {
    return fmtstr("%s[%s]", TOKEN_NAMES[tok.kind], tok.identifier_name);
  }
  return TOKEN_NAMES[peek(cont).kind];
}

void backtrack_to(ParserCont *cont, int new_pos) {
  cont->pos = new_pos;
}

#define consume(cont) do { \
  THROW_IF(cont->pos == cont->tokens_size, EXC_PARSE_SYNTAX, "EOF reached without finishing parse"); \
  DEBUG_PRINT_EXPR("consumed %s", peek_str(cont)); \
  cont->pos++; \
} while (0)

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
void *parse_expr(ParserCont *cont);

void *parse_primary_expr(ParserCont *cont) {
  PRINT_ENTRY();
  Token tok = peek(cont);
  void *ret;
  switch (tok.kind) {
    case TOK_IDENT:
      consume(cont);
      return get_symbol(cont->symtab, tok.string_id);
    case TOK_FLOAT_LITERAL:
      consume(cont);
      return cont->visitor->visit_float_literal(cont->visitor, tok.double_val);
    case TOK_INTEGER_LITERAL:
      consume(cont);
      return cont->visitor->visit_integer_literal(cont->visitor, tok.int64_val);
    case TOK_LEFT_PAREN:
      consume(cont);
      ret = parse_expr(cont);
      if (peek(cont).kind != TOK_RIGHT_PAREN) {
        THROWF(EXC_PARSE_SYNTAX, "Expected ) but got %s", peek_str(cont));
      }
      consume(cont);
      return ret;
    default:
      THROWF(EXC_PARSE_SYNTAX, "Unexpected token: %s", TOKEN_NAMES[tok.kind]);
  }
}

void *parse_postfix_expr(ParserCont *cont) {
  PRINT_ENTRY();
  return parse_primary_expr(cont);
}

void *parse_unary_expr(ParserCont *cont) {
  PRINT_ENTRY();
  void *ret = parse_postfix_expr(cont);
  // cont->visitor->set_property(ret, PROP_IS_UNARY_EXPR, 1);
  return ret;
}

void *parse_cast_expr(ParserCont *cont) {
  PRINT_ENTRY();
  return(parse_unary_expr(cont));
}

#define MAKE_BINOP_PARSER(parse_func, next_parse_func, continue_pred) \
void *parse_func(ParserCont *cont) { \
  PRINT_ENTRY(); \
  void *left = next_parse_func(cont); \
  void *right = 0; \
  for (;;) { \
    Token op = peek(cont); \
    if (!continue_pred(op)) \
      break; \
    consume(cont); \
    right = next_parse_func(cont); \
    left = cont->visitor->visit_binop(cont->visitor, op.kind, left, right); \
  } \
  return left; \
}

#define multiplicative_pred(op) (op.kind == TOK_STAR_OP || op.kind == TOK_DIV_OP || op.kind == TOK_MOD_OP)
MAKE_BINOP_PARSER(parse_multiplicative_expr, parse_cast_expr, multiplicative_pred)

#define additive_pred(op) (op.kind == TOK_ADD_OP || op.kind == TOK_SUB_OP)
MAKE_BINOP_PARSER(parse_additive_expr, parse_multiplicative_expr, additive_pred)

void *parse_shift_expr(ParserCont *cont) {
  PRINT_ENTRY();
  return parse_additive_expr(cont);
}

void *parse_relational_expr(ParserCont *cont) {
  PRINT_ENTRY();
  return parse_shift_expr(cont);
}

void *parse_equality_expr(ParserCont *cont) {
  PRINT_ENTRY();
  return parse_relational_expr(cont);
}

void *parse_and_expr(ParserCont *cont) {
  PRINT_ENTRY();
  return parse_equality_expr(cont);
}

void *parse_exclusive_or_expr(ParserCont *cont) {
  PRINT_ENTRY();
  return parse_and_expr(cont);
}

void *parse_inclusive_or_expr(ParserCont *cont) {
  PRINT_ENTRY();
  return parse_exclusive_or_expr(cont);
}

void *parse_logical_and_expr(ParserCont *cont) {
  PRINT_ENTRY();
  return parse_inclusive_or_expr(cont);
}

void *parse_logical_or_expr(ParserCont *cont) {
  PRINT_ENTRY();
  return parse_logical_and_expr(cont);
}

void *parse_conditional_expr(ParserCont *cont) {
  PRINT_ENTRY();
  return parse_logical_or_expr(cont);
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
  int int_size = -1, int_alignment = -1;
  switch (parsed_long_short) {
    case -1: int_size = cont->sizes.short_size; int_alignment = cont->alignments.short_size; break;
    case 0: int_size = cont->sizes.int_size; int_alignment = cont->alignments.int_size; break;
    case 1: int_size = cont->sizes.long_size; int_alignment = cont->alignments.long_size; break;
    case 2: int_size = cont->sizes.long_long_size;  int_alignment = cont->alignments.long_long_size;break;
    default: assert(0 && "Unreachable!");
  }
  if (parsed_type_tok == TOK_char) {
    int_size = cont->sizes.char_size; int_alignment = cont->alignments.char_size;
  }
  assert(int_size != -1 && "Oops");
  assert(int_alignment != -1 && "Oops");

  Type *ty = checked_calloc(1, sizeof(Type));
  switch (parsed_type_tok) {
    case TOK_void: ty->kind = TY_VOID; break;
    case TOK_char: case TOK_int: ty->kind = TY_INTEGER; ty->size = int_size; ty->alignment = int_alignment; ty->is_unsigned = is_unsigned; break;
    case TOK_float: ty->kind = TY_FLOAT; ty->size = cont->sizes.float_size; ty->alignment = cont->alignments.float_size; break;
    case TOK_double:
      ty->kind = TY_FLOAT;
      ty->size = (parsed_long_short == 1 ? cont->sizes.long_double_size : cont->sizes.double_size);
      ty->alignment = (parsed_long_short == 1 ? cont->alignments.long_double_size : cont->alignments.double_size);
      break;
    default: assert(0 && "Unreachable!");
  }
  return (DeclarationSpecifiers) {.type = ty};
}

#define is_assignment_op(op) tok_is_in( \
  op, TOK_ASSIGN_OP, TOK_MUL_ASSIGN, TOK_MOD_ASSIGN, TOK_ADD_ASSIGN, TOK_SUB_ASSIGN, TOK_LEFT_ASSIGN, \
  TOK_RIGHT_ASSIGN, TOK_AND_ASSIGN, TOK_XOR_ASSIGN, TOK_OR_ASSIGN \
)

// An assignment_expr is either a conditional_expr or starts with an unary_expr. To resolve the ambiguity, notice that
// a conditional_expr expands to an unary_expr. Therefore, we start parsing as a conditional_expr and then check if the
// returned value happens to be an unary_expr (TODO). The visitor will have to keep track of this in visit_cast_expr.

// TODO: The logic above is incorrect; you should make a fully predictive parser.
// NB last note: FIRST(cast_expression) is (. Predictive parser is to just look for a (. 

void *parse_assignment_expr(ParserCont *cont) {
  PRINT_ENTRY();
  void *left = parse_conditional_expr(cont);  // which could be an unary expression
  void *right = 0;
  for (;;) {
    TokenKind op = peek(cont).kind;
    if (!is_assignment_op(op))
      break;
    // Now we know this is an assignment
    consume(cont);
    right = parse_assignment_expr(cont);
    left = cont->visitor->visit_assign(cont->visitor, op, left, right);
  }
  return left;
}

Declarator *parse_declarator(ParserCont *cont) {
  PRINT_ENTRY();
  Declarator *ret = calloc(1, sizeof(Declarator));

  if (peek(cont).kind == TOK_STAR_OP) {
    ret->is_pointer = 1;
    consume(cont);
  }
  if (peek(cont).kind == TOK_LEFT_PAREN) {
    consume(cont);
    ret = parse_declarator(cont);
    THROW_IF(peek(cont).kind != TOK_RIGHT_PAREN, EXC_PARSE_SYNTAX, "Expected ) to close declarator");
    consume(cont);
    return ret;
  }
  THROW_IF(peek(cont).kind != TOK_IDENT, EXC_PARSE_SYNTAX, "Expected identifier");
  Token ident = peek(cont);
  ret->kind = DC_SCALAR;  // scalar until proven otherwise
  ret->ident_string_id = ident.string_id;
  ret->ident_string = ident.identifier_name;
  consume(cont);

  DECLARE_VECTOR(DeclarationSpecifiers, param_decl_specs)
  DECLARE_VECTOR(Declarator *, param_declarators)

  NEW_VECTOR(param_decl_specs, sizeof(DeclarationSpecifiers));
  NEW_VECTOR(param_declarators, sizeof(Declarator *));
  
  if (peek(cont).kind == TOK_LEFT_PAREN) {
    // parse function declarator
    ret->kind = DC_FUNCTION;
    consume(cont);

    for (;;) {
      DeclarationSpecifiers curr_decl_specs = parse_declaration_specifiers(cont);
      Declarator *curr_declarator = parse_declarator(cont);
      APPEND_VECTOR(param_decl_specs, curr_decl_specs);
      APPEND_VECTOR(param_declarators, curr_declarator);

      if (peek(cont).kind != TOK_COMMA)
        break;

      consume(cont);
    }
    assert(VECTOR_SIZE(param_decl_specs) == VECTOR_SIZE(param_declarators));
    THROW_IF(peek(cont).kind != TOK_RIGHT_PAREN, EXC_PARSE_SYNTAX, "Expected ) to close function declarator");

    consume(cont);
    ret->n_params = VECTOR_SIZE(param_decl_specs);
    ret->param_decl_specs = param_decl_specs;
    ret->param_declarators = param_declarators;
  }
  // arrays not supported
  return ret;
}

void *parse_initializer(ParserCont *cont) {
  PRINT_ENTRY();
  return parse_assignment_expr(cont);
}

void *visit_and_insert_declaration(ParserCont *cont, DeclarationSpecifiers decl_specs, Declarator *declarator) {
    void *declaration = cont->visitor->visit_declaration(cont->visitor, decl_specs, declarator);
    insert_symbol(cont->symtab, declarator->ident_string_id, decl_specs, declaration);
    return declaration;
}

void parse_declaration_rest(ParserCont *cont, DeclarationSpecifiers decl_specs, Declarator *first_declarator) {
  PRINT_ENTRY();
  Declarator *declarator = first_declarator;
  for (;;) {
    TokenKind op = peek(cont).kind;
    switch (op) {
      case TOK_SEMI:
        consume(cont);
        visit_and_insert_declaration(cont, decl_specs, declarator);
        return;
      case TOK_COMMA:
        consume(cont);
        visit_and_insert_declaration(cont, decl_specs, declarator);
        declarator = parse_declarator(cont);
        break;  // more declarators to come
      case TOK_ASSIGN_OP:
        consume(cont);
        void *lhs = visit_and_insert_declaration(cont, decl_specs, declarator);
        // TODO: Support initializer lists differently... it won't "just" be delegating to visit_assign.
        void *rhs = parse_initializer(cont);
        cont->visitor->visit_assign(cont->visitor, TOK_ASSIGN_OP, lhs, rhs);
        // consume the comma there, to prevent the previous rule from declaring twice
        if (peek(cont).kind == TOK_COMMA)
          consume(cont);
        break;
      default:
        THROWF(EXC_PARSE_SYNTAX, "Unexpected token %s", TOKEN_NAMES[op]);
    }
  }
}

#define is_declaration_first(op) is_declaration_specifier_first(op)
void parse_declaration(ParserCont *cont) {
  PRINT_ENTRY();
  DeclarationSpecifiers decl_specs = parse_declaration_specifiers(cont);
  Declarator *first_declarator = parse_declarator(cont);
  parse_declaration_rest(cont, decl_specs, first_declarator);
}


void *parse_expression_statement(ParserCont *cont) {
  PRINT_ENTRY();
  void *expr = parse_expr(cont);
  THROW_IF(peek(cont).kind != TOK_SEMI, EXC_PARSE_SYNTAX, "expected ;");
  consume(cont);
  return expr;
}

#define is_jump_statement_first(op) tok_is_in(op, TOK_goto, TOK_continue, TOK_break, TOK_return)
void parse_jump_statement(ParserCont *cont) {
  TokenKind op = peek(cont).kind;
  void *retval = 0;
  switch (op) {
    case TOK_return:
      consume(cont);
      if (peek(cont).kind != TOK_SEMI) {
        retval = parse_expr(cont);
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
  CALL(cont->visitor, visit_function_definition_start, decl_specs, func_declarator);
  push_new_symbol_table(cont);
  for (int i = 0; i < func_declarator->n_params; i++) {
    DeclarationSpecifiers param_decl_specs = func_declarator->param_decl_specs[i];
    Declarator *param_declarator = func_declarator->param_declarators[i];
    void *param_value = CALL(
      cont->visitor,
      visit_function_definition_param,
      param_decl_specs,
      param_declarator
    );
    insert_symbol(cont->symtab, param_declarator->ident_string_id, param_decl_specs, param_value);
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
  Declarator *first_declarator = parse_declarator(cont);

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
#define expr_pred(op) (op.kind == TOK_COMMA)
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
    exit(1);
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
