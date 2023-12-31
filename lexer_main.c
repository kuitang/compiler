#include <stdio.h>
#include "lexer.h"
#include "common.h"

static void parse_start(FILE *in, const char *filename) {
  ScannerCont *cont = new_scanner_cont(in, filename);
  if (setjmp(global_exception_handler) == 0) {
    while (1) {
      Token tok = consume_next_token(cont);
      if (tok.kind == TOK_END_OF_FILE) {
        break;
      }
      printf(
        "%d\t%d\t%d\t%d\t%s\t",
        tok.line_start + 1, tok.col_start + 1, tok.line_end + 1, tok.col_end + 1,
        // "%s\t",
        TOKEN_NAMES[tok.kind]
      );
      switch (tok.kind) {
        case TOK_STRING_LITERAL: case TOK_IDENT:
          printf("%d:%s", tok.string_id, tok.string_val);
          break;
        case TOK_INTEGER_LITERAL:
          printf("%lld", tok.int64_val);
          break;
        case TOK_FLOAT_LITERAL:
          printf("%g", tok.double_val);
          break;
        default:
          break;
      }
      printf("\n");
    }
  } else {
    PRINT_EXCEPTION();
    exit(1);
  }
  fprint_string_pool(stdout, cont);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "gen_lexer: no input file\n");
    exit(1);
  }
  // printf("punctations:\n");
  // for (int i = 0; i < N_PUNCTS; i++) {
  //   printf("%8d: %s\n", i, PUNCT_VALUES[i]);
  // }
  // printf("keywords:\n");
  // for (int i = 0; i < N_KEYWORDS; i++) {
  //   printf("%8d: %s\n", i, KEYWORD_VALUES[i]);
  // }

  FILE *in = fopen(argv[1], "r");
  DIE_IF(!in, "Couldn't open input file");
  init_lexer_module();

  parse_start(in, argv[1]);
  return 0;
}
