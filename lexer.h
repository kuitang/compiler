#pragma once
#include "tokens.h"
#include "common.h"
#include "vendor/klib/khash.h"

KHASH_MAP_INIT_STR(string, int)
typedef struct {
  DECLARE_VECTOR(const char *, string_val)
  khash_t(string) *string_id;
} StringPool;

typedef struct {
  // static
  const char *filename;
  const char *buf;
  int size;
  // dynamic
  int pos;
  int line;
  int col;
  // saved at the start of a parse
  int saved_pos;
  int saved_line;
  int saved_col;
  StringPool *string_pool;
} ScannerCont;

typedef enum {
  CONST_STRING,
  CONST_INTEGER,
  CONST_FLOAT,
} ConstantKind;

typedef struct {
  TokenKind kind;
  union {
    int string_id;
    int64_t int64_val;
    double double_val;
  };
  // debugging
  const char *identifier_name;
  const char *filename;
  int pos_start;
  int pos_end;
  int line_start;
  int line_end;
  int col_start;
  int col_end;
} Token;

StringPool *new_string_pool();
ScannerCont make_scanner_cont(FILE *in, const char *filename, StringPool *string_pool);
Token consume_next_token(ScannerCont *cont);
void fprint_string_repr(FILE *out, const char *s);
void fprint_string_pool(FILE *f, StringPool *pool);

void init_lexer_module();
