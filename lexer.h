#pragma once
#include "tokens.h"
#include "common.h"

typedef struct ScannerCont ScannerCont;

typedef struct {
  TokenKind kind;
  union {
    int string_id;
    int64_t int64_val;
    double double_val;
  };
  // debugging
  const char *string_val;  // TODO: Rename to string value and replace string_id with struct. Then rest of code doesn't need stringpool.
  const char *filename;
  int pos_start;
  int pos_end;
  int line_start;
  int line_end;
  int col_start;
  int col_end;
} Token;

ScannerCont *new_scanner_cont(FILE *in, const char *filename);
Token consume_next_token(ScannerCont *cont);
void init_lexer_module();
/** Print contents of string pool to f, for debugging */
void fprint_string_pool(FILE *f, ScannerCont *cont);
