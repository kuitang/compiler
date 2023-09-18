#include "lexer.h"
#include "parser.h"
#include <stdio.h>
#include "common.h"
#include "tokens.h"
#include <assert.h>
#include <unistd.h>

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

static void consume(ParserCont *cont) {
  THROW_IF(cont->pos == cont->tokens_size, EXC_LEX_SYNTAX, "EOF reached without finishing parse");
  cont->pos++;
}

ParserCont make_parser_cont(ScannerCont *scont, VisitorHeader *visitor) {
  ParserCont ret = {
    .scont = scont,
    .visitor = visitor,
  };
  NEW_VECTOR(ret.tokens, sizeof(Token));
  return ret;
}

#define peek(cont) (cont)->tokens[(cont)->pos]
#define peek_str(cont) TOKEN_NAMES[peek(cont).kind]
// #define is_parser_eof(cont) (cont)->pos == (cont)->tokens_size;

#define PRINT_ENTRY() \
  fprintf(stderr, "> peeking %s at %d:%d inside %s\n", peek_str(cont), peek(cont).line_start, peek(cont).col_start, __func__);


// parse_primary_expr needs this forward declaration
void *parse_expr(ParserCont *cont);

void *parse_primary_expr(ParserCont *cont) {
  PRINT_ENTRY();
  Token tok = peek(cont);
  void *ret;
  switch (tok.kind) {
    case TOK_IDENT:
      consume(cont);
      return cont->visitor->visit_ident(cont->visitor, tok.string_id);
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
  return parse_postfix_expr(cont);
}

void *parse_cast_expr(ParserCont *cont) {
  PRINT_ENTRY();
  return(parse_unary_expr(cont));
}

// once this multiplies a lot more, make a macro like this
// #define MAKE_LEFT_RECURSIVE_PARSER(parse_func, next_parse_func, cond)

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

/*
assignment_expression
  : conditional_expression
  | unary_expression assignment_operator assignment_expression

Backtracking is unavoidable because both conditional_expression also eventually expands to unary_expression, so we
cannot disambiguate with one token of lookahead.
*/
/*
int is_primary_expr_op(TokenKind op) {
  switch (op) {
    case TOK_IDENT: case TOK_INTEGER_LITERAL: case TOK_STRING_LITERAL: case TOK_LEFT_PAREN: case TOK__Generic:
      return 1;
    default:
      return 0;
  }
}

int is_postfix_expr_op(TokenKind op) {
  return is_primary_expr_op(op) || op == TOK_LEFT_BRACE || op == TOK_LEFT_PAREN || op == TOK_DOT_OP || op == TOK_PTR_OP ||
    op == TOK_INC_OP || op == TOK_DEC_OP;
}

int is_unary_op(TokenKind op) {
  switch (op) {
    case TOK_AMPERSAND_OP: case TOK_STAR_OP: case TOK_ADD_OP: case TOK_SUB_OP: case TOK_COMPL_OP: case TOK_NOT_OP:
      return 1;
    default:
      return 0;
  }
}

// This is no good; only those that CANNOT appear in conditional!
int is_unary_expr_op(TokenKind op) {
  return is_postfix_expr_op(op) || is_unary_op(op) || op == TOK_INC_OP || op == TOK_DEC_OP || is_unary_op(op) ||
    op == TOK_sizeof || op == TOK_alignof
  ;
}

int is_unary_expr_only_op(TokenKind op) {
  switch (op) {
    case TOK_INC_OP: case TOK_DEC_OP: case TOK_sizeof: case TOK_alignof:
      return 1;
    default:
      return is_unary_op(op);
  }
}
*/

int is_assignment_op(TokenKind op) {
  switch (op) {
    case TOK_ASSIGN_OP: case TOK_MUL_ASSIGN: case TOK_MOD_ASSIGN: case TOK_ADD_ASSIGN: case TOK_SUB_ASSIGN:
    case TOK_LEFT_ASSIGN: case TOK_RIGHT_ASSIGN: case TOK_AND_ASSIGN: case TOK_XOR_ASSIGN: case TOK_OR_ASSIGN:
      return 1;
    default:
      return 0;
  }
}

// An assignment_expr is either a conditional_expr or starts with an unary_expr. To resolve the ambiguity, notice that
// a conditional_expr expands to an unary_expr. Therefore, we start parsing as a conditional_expr and then check if the
// returned value happens to be an unary_expr (TODO). The visitor will have to keep track of this in visit_cast_expr.

void *parse_assignment_expr(ParserCont *cont) {
  PRINT_ENTRY();
  void *left = parse_conditional_expr(cont);
  void *right = 0;
  for (;;) {
    Token op = peek(cont);
    if (!(cont->visitor->is_unary_expr(cont->visitor, left) && is_assignment_op(op.kind)))
      break;
    consume(cont);
    right = parse_assignment_expr(cont);
    left = cont->visitor->visit_binop(cont->visitor, op.kind, left, right);
  }
  return left;
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
      if (tok.kind == TOK_END_OF_FILE)
        break;
      APPEND_VECTOR(cont.tokens, tok);
    }
    void *result = parse_expr(&cont);
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

    if (cont.pos != cont.tokens_size) {
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

#undef peek
#undef peek_str
#undef PRINT_ENTRY
#undef THROWF_SYNTAX
