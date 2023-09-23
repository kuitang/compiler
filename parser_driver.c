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

static char *outfile = 0;

// Main reference: https://en.wikipedia.org/wiki/Tail_recursive_parser

// Eventually you need to make lexing and parsing concurrent... which is fine because you have just one lookahead.
// Dear god will do typedefs later
// See https://stackoverflow.com/questions/59482460/how-to-handle-ambiguity-in-syntax-like-in-c-in-a-parsing-expression-grammar-l#:~:text=The%20well%2Dknown%20%22typedef%20problem,to%20the%20lexer%20during%20parsing.

// [x86_64ABI Figure 3.1] https://refspecs.linuxbase.org/elf/x86_64-abi-0.98.pdf
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
  int index;
  int offset;
  const Type *type;
} Symbol;

KHASH_MAP_INIT_INT(SymbolTableMap, Symbol *)
typedef struct SymbolTable {
  kh_SymbolTableMap_t *map;
  int curr_index;
  int curr_offset;
  struct SymbolTable *parent;
} SymbolTable;

SymbolTable *new_symbol_table() {
  SymbolTable *ret = checked_malloc(sizeof(SymbolTable));
  ret->map = kh_init_SymbolTableMap();
  ret->curr_index = 0;
  ret->curr_offset = 0;
  return ret;
}

int next_aligned_offset(int curr_offset, int size, int alignment) {
  int new_offset = curr_offset + size;
  new_offset += new_offset % alignment;
  return new_offset;
}

const Symbol *get_symbol(const struct SymbolTable *tab, int string_id) {
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

struct SymbolTable;
typedef struct {
  ScannerCont *scont;  
  int depth;
  int pos;  // current token
  MachineSizes sizes;
  MachineSizes alignments;
  struct VisitorHeader *visitor;
  struct SymbolTable *symtab;
  DECLARE_VECTOR(Token, tokens)
} ParserCont;

ParserCont make_parser_cont(ScannerCont *scont, VisitorHeader *visitor) {
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

SymbolTable *push_new_symbol_table(ParserCont *cont, int inherit_offset) {
  SymbolTable *ret = new_symbol_table();
  ret->parent = cont->symtab;
  if (inherit_offset) {
    ret->curr_offset = cont->symtab->curr_offset;
  }
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
  Symbol *x;
  kh_foreach_value(top_tab->map, x, free(x))
  kh_destroy_SymbolTableMap(top_tab->map);
  cont->symtab = top_tab->parent;
  free(top_tab);
}

const Symbol *insert_symbol(ParserCont *cont, int string_id, const Type *type) {
  SymbolTable *tab = cont->symtab;
  khiter_t map_iter = kh_get_SymbolTableMap(tab->map, string_id);
  THROWF_IF(map_iter != kh_end(tab->map), EXC_PARSE_SYNTAX, "symbol string_id %d is already defined in current scope", string_id);
  int ret;
  map_iter = kh_put_SymbolTableMap(tab->map, string_id, &ret);
  THROW_IF(ret == -1, EXC_SYSTEM, "kh_put failed");

  Symbol *new_symbol = checked_calloc(1, sizeof(Symbol));
  new_symbol->index = tab->curr_index;
  new_symbol->offset = tab->curr_offset;
  new_symbol->type = type;
  kh_val(tab->map, map_iter) = new_symbol;

  tab->curr_index++;
  tab->curr_offset = next_aligned_offset(tab->curr_offset, type->size, type->alignment);

  // debug
  khiter_t get_iter = kh_get_SymbolTableMap(tab->map, string_id);
  THROWF_IF(
    get_iter == kh_end(tab->map),
    EXC_INTERNAL,
    "could not get string_id %d back from the symbol map hashtable", string_id
  );
  Symbol *get_value = kh_val(tab->map, string_id);
  printf(
    "inserted into symbol table %p at index %d, offset %d: %s\n",
    (void *) tab,
    get_value->index,
    get_value->offset,
    type_to_json(get_value->type)
  );
  return new_symbol;
}

// Helpers
// #define get_string(cont, id) cont->scont->string_pool->string_id[id]
// these are functions instead of macros so we can call them in the debugger
Token peek(ParserCont *cont) {
  return (cont)->tokens[(cont)->pos];
}

const char *peek_str(ParserCont *cont) {
  return TOKEN_NAMES[peek(cont).kind];
}

void backtrack_to(ParserCont *cont, int new_pos) {
  cont->pos = new_pos;
}

static void consume(ParserCont *cont) {
  THROW_IF(cont->pos == cont->tokens_size, EXC_PARSE_SYNTAX, "EOF reached without finishing parse");
  DEBUG_PRINT_EXPR("consumed %s", peek_str(cont));
  cont->pos++;
}

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

// parse_primary_expr needs this forward declaration
void *parse_expr(ParserCont *cont);

void *parse_primary_expr(ParserCont *cont) {
  PRINT_ENTRY();
  Token tok = peek(cont);
  void *ret;
  const Symbol *sym;
  switch (tok.kind) {
    case TOK_IDENT:
      sym = get_symbol(cont->symtab, tok.string_id);
      THROWF_IF(!sym, EXC_PARSE_SYNTAX, "identifier %d not found in any symbol table", tok.string_id);
      consume(cont);
      return cont->visitor->visit_ident(cont->visitor, sym->type, sym->index, sym->offset);
    case TOK_FLOAT_LITERAL:
      consume(cont);
      return cont->visitor->visit_float_literal(cont->visitor, tok.constant_val.double_val);
    case TOK_INTEGER_LITERAL:
      consume(cont);
      return cont->visitor->visit_integer_literal(cont->visitor, tok.constant_val.int64_val);
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
  cont->visitor->set_property(ret, PROP_IS_UNARY_EXPR, 1);
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
#define is_declaration_first(op) is_declaration_specifier_first(op)

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
  return (DeclarationSpecifiers) {.type = ty, .some_flag = 0xDEADBEEF};
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
  void *left = parse_conditional_expr(cont);
  void *right = 0;
  for (;;) {
    Token op = peek(cont);
    int left_is_unary_expr = cont->visitor->get_property(left, PROP_IS_UNARY_EXPR);
    if (!(left_is_unary_expr && is_assignment_op(op.kind)))
      break;
    // Now we know this is an assignment
    consume(cont);
    right = parse_assignment_expr(cont);
    left = cont->visitor->visit_assign(cont->visitor, left, right);
  }
  return left;
}

// only support identifier
void *parse_declarator(ParserCont *cont, DeclarationSpecifiers decl_specs) {
  PRINT_ENTRY();
  Token ident = peek(cont);
  THROW_IF(ident.kind != TOK_IDENT, EXC_PARSE_SYNTAX, "only identifier declarators are supported for now.");
  consume(cont);
  const Symbol *sym = insert_symbol(cont, ident.string_id, decl_specs.type);
  return cont->visitor->visit_declarator(cont->visitor, sym->index, sym->offset);
}

typedef struct {
  int success;
  int function_name;
  int params_have_names;
  DECLARE_VECTOR(int, param_names)
  DECLARE_VECTOR(DeclarationSpecifiers, param_specifiers)
} FunctionDefinitionDeclarator;

// pretty sure ident is the ONLY valid kind of declarator; rename to emphasize function definition?
FunctionDefinitionDeclarator parse_function_definition_declarator(ParserCont *cont) {
  PRINT_ENTRY()
  // no pointers or parentheses funny business; dear god it'll be fun to parse function pointer definitions
  FunctionDefinitionDeclarator ret = { .success = 0, .params_have_names = 0 };  // i.e. failure
  NEW_VECTOR(ret.param_names, sizeof(int));
  NEW_VECTOR(ret.param_specifiers, sizeof(DeclarationSpecifiers));

  Token ident = peek(cont);
  if (ident.kind != TOK_IDENT)
    return ret;  // fail
  ret.function_name = ident.string_id;
  consume(cont);
  if (peek(cont).kind != TOK_LEFT_PAREN) {
    // no opening paren => not a function declarator => fail
    return ret;
  }
  consume(cont);

  DeclarationSpecifiers declaration_specifiers;
  int param_string_id;
  for (;;) {
    declaration_specifiers = parse_declaration_specifiers(cont);
    APPEND_VECTOR(ret.param_specifiers, declaration_specifiers);
    if (peek(cont).kind == TOK_IDENT) {
      param_string_id = peek(cont).string_id;
      APPEND_VECTOR(ret.param_names, param_string_id);
      consume(cont);
    } else {
      ret.params_have_names = 0;
    }

    // parsed the parameter declaration; now look for , or )
    switch (peek(cont).kind) {
      case TOK_COMMA:
        consume(cont);
        break;
      case TOK_RIGHT_PAREN:
        consume(cont);
        // finish the declarator
        assert(!ret.params_have_names || ret.param_specifiers_size == ret.param_names_size);
        ret.success = 1;
        return ret;
      case TOK_END_OF_FILE:
        THROW(EXC_PARSE_SYNTAX, "Unmatched (");
      default:
        break;
    }
  }
  assert(0 && "Unreachable!");
}

void *parse_init_declarator_list(ParserCont *cont, DeclarationSpecifiers declaration_specifiers) {
  PRINT_ENTRY();
  // only support list of declarators for now
  void *declarator = parse_declarator(cont, declaration_specifiers);
  for (;;) {
    if (peek(cont).kind != TOK_COMMA)
      break;
    declarator = parse_declarator(cont, declaration_specifiers);
  }
  return declarator;
}

// TODO: Make compatible with AST visitor; viz put prev in everything... or do something else?
void *parse_declaration(ParserCont *cont) {
  PRINT_ENTRY();
  DeclarationSpecifiers declaration_specifiers = parse_declaration_specifiers(cont);
  fprintf(stdout, "parsed declaration_specifiers (really just type): %s\n", type_to_json(declaration_specifiers.type));
  void *ret = parse_init_declarator_list(cont, declaration_specifiers);
  THROW_IF(peek(cont).kind != TOK_SEMI, EXC_PARSE_SYNTAX, "expected ; to finish declaration");
  consume(cont);
  return ret;
}

void *parse_expression_statement(ParserCont *cont) {
  void *expr = parse_expr(cont);
  THROW_IF(peek(cont).kind != TOK_SEMI, EXC_PARSE_SYNTAX, "expected ;");
  consume(cont);
  return expr;
}

void *parse_compound_statement(ParserCont *cont);

#define is_compound_statement_first(op) tok_is_in(op, TOK_LEFT_BRACE)
void *parse_statement(ParserCont *cont) {
  PRINT_ENTRY()
  TokenKind op = peek(cont).kind;
  if (is_compound_statement_first(op))
    return parse_compound_statement(cont);
  return parse_expression_statement(cont);
}

void *parse_block_item(ParserCont *cont) {
  PRINT_ENTRY()
  // this can be parsed predictively
  TokenKind op = peek(cont).kind;
  if (is_declaration_first(op))
    return parse_declaration(cont);

  return parse_statement(cont);
}

void *parse_compound_statement(ParserCont *cont) {
  PRINT_ENTRY()
  THROW_IF(peek(cont).kind != TOK_LEFT_BRACE, EXC_PARSE_SYNTAX, "expected {");
  consume(cont);

  void *block_item = 0;
  for (;;) {
    // could have 0 items
    if (peek(cont).kind == TOK_RIGHT_BRACE) {
      consume(cont);
      return block_item;
    }
    block_item = parse_block_item(cont);
  }

  THROW_IF(peek(cont).kind != TOK_RIGHT_BRACE, EXC_PARSE_SYNTAX, "expected }");
}

void *parse_function_definition(ParserCont *cont) {
  PRINT_ENTRY()
  DeclarationSpecifiers declaration_specifiers = parse_declaration_specifiers(cont);
  // assert specifiers don't contain other stuff...
  FunctionDefinitionDeclarator declarator = parse_function_definition_declarator(cont);
  if (!declarator.success)
    return 0;

  Type *function_type = checked_calloc(1, sizeof(Type));
  int n_params = declarator.param_specifiers_size;
  function_type->kind = TY_FUNCTION;
  function_type->n_params = n_params;
  function_type->return_type = declaration_specifiers.type;
  function_type->declaration_specifiers = checked_calloc(n_params, sizeof(DeclarationSpecifiers));
  checked_memcpy(function_type->declaration_specifiers, declarator.param_specifiers, n_params * sizeof(DeclarationSpecifiers));
  // TODO: free!
  
  push_new_symbol_table(cont, 1 /* inherit_offset = 1 for a function */);
  for (int i = 0; i < n_params; i++) {
    insert_symbol(cont, declarator.param_names[i], declarator.param_specifiers[i].type);
  }
  // void *prototype = cont->visitor->visit_function_definition(
  cont->visitor->visit_function_definition(
    cont->visitor,
    declarator.function_name,
    function_type
  );
  return parse_compound_statement(cont);
}

void *parse_external_declaration(ParserCont *cont) {
  PRINT_ENTRY();
  int saved_pos = cont->pos;
  // functions_definition and declarations could both begin with declaration_specifiers declaration_declarator, but
  // the declarations nonterminal will eventually produce ; while the function_definition terminal will eventually
  // produce a compound_statement. Therefore, we first try to parse as a function declaration and backtrack and try
  // declaration if we fail. This is safe because parse_function_definition will not generate any code unless it
  // succeeds.
  void *function = parse_function_definition(cont);
  if (function)
    return function;
  
  backtrack_to(cont, saved_pos);
  return parse_declaration(cont);
}

void *parse_translation_unit(ParserCont *cont) {
  PRINT_ENTRY()
  void *external_declaration = parse_external_declaration(cont);
  while (peek(cont).kind != TOK_END_OF_FILE) {
    external_declaration = parse_external_declaration(cont);
  }
  return external_declaration; 
}

// Comma evaluates the first operand, discards the result, then evaluates the second operand and returns the result.
// If none of the operands had side effects, then we don't need to call any visit method. And for instruction emitters
// like SSA, the code to produce the side effects were already emitted. However, for the AST emitter, we still need to
// visit so that all of the operands get joined to the tree.
//
// So in the end this is just any other binary operator.
#define expr_pred(op) (op.kind == TOK_COMMA)
MAKE_BINOP_PARSER(parse_expr, parse_assignment_expr, expr_pred)

extern VisitorHeader *new_ssa_visitor();
extern VisitorHeader *new_ast_visitor();
extern VisitorHeader *new_x86_64_visitor();

static void parse_start(FILE *in, const char *filename, VisitorHeader *visitor) {
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
    // void *result = parse_expr(&cont);
    void *result = parse_translation_unit(&cont);
    printf("The final result is:\n\n");
    visitor->dump_result(stdout, result);
    printf("\n\nThe state of the visitor at the end was:\n\n");
    visitor->dump(visitor, stdout);

    if (outfile) {
      FILE *out = fopen(outfile, "w");
      // TODO: checked_fopen
      DIE_IF(!in, "Couldn't open output file");
      visitor->dump(visitor, out);
      // fclose(out);
      // out = 0;
    }
    // printf("The output above was writen to %s\n", outfile);

    if (cont.pos != cont.tokens_size && peek(&cont).kind != TOK_END_OF_FILE) {
      fprintf(stderr, "ERROR: Parser did not reach EOF; next token is %s\n", peek_str(&cont));
    }
  } else {
    PRINT_EXCEPTION();
    exit(1);
  }
  fprint_string_pool(stdout, pool);
}

void init_parser_module() {
  init_lexer_module();
}

void usage() {
  fprintf(stderr, "usage: parser_driver [options] file\n\n");
  fprintf(stderr, "options:\n");
  fprintf(stderr, "  -v <visitor> which visitor to use (choices: ssa, ast, x86_64)\n");
  fprintf(stderr, "  -o <file>    save output to this file");
  exit(1);
}

// hack
int main(int argc, char *argv[]) {
  VisitorConstructor visitor_ctor = 0;
  int ch;
  int optarg_len;
  while ((ch = getopt(argc, argv, "v:o:")) != -1) {
    switch (ch) {
      case 'v':
        if (strcmp(optarg, "ssa") == 0) {
          visitor_ctor = new_ssa_visitor;
          break;
        } else if (strcmp(optarg, "ast") == 0) {
          visitor_ctor = new_ast_visitor;
          break;
        } else if (strcmp(optarg, "x86_64") == 0) {
          visitor_ctor = new_x86_64_visitor;
          break;
        }
      case 'o':
        optarg_len = strlen(optarg);
        outfile = checked_malloc(optarg_len + 1);
        strlcpy(outfile, optarg, optarg_len + 1);
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

  if (!visitor_ctor) {
    visitor_ctor = new_ssa_visitor;  // or should it be ast?
  }

  // printf("punctations:\n");
  // for (int i = 0; i < N_PUNCTS; i++) {
  //   printf("%8d: %s\n", i, PUNCT_VALUES[i]);
  // }
  // printf("keywords:\n");
  // for (int i = 0; i < N_KEYWORDS; i++) {
  //   printf("%8d: %s\n", i, KEYWORD_VALUES[i]);
  // }

  FILE *in = fopen(argv[0], "r");
  DIE_IF(!in, "Couldn't open input file");
  init_parser_module();

  parse_start(in, argv[0], visitor_ctor());
  return 0;
}

#undef PRINT_ENTRY
