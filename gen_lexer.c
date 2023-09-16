#include "tokens.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <ctype.h>

#include "vendor/klib/khash.h"
#include "common.h"

#define ptr_array_len(arr) sizeof(arr) / sizeof(void *)
#define max(x, y) (x) > (y) ? (x) : (y)
#define fill_lens(storage) do { \
  max_##storage##_len = -1; \
  for (int _i = 0; _i < n_##storage; _i++) { \
    storage##_len[_i] = strlen(storage[_i]); \
    max_##storage##_len = max(storage##_len[_i], max_##storage##_len); \
  } \
} while (0)
static int max_PUNCT_VALUES_len = -1;
static int PUNCT_VALUES_len[N_PUNCTS];

static int max_KEYWORD_VALUES_len = -1;
static int KEYWORD_VALUES_len[N_KEYWORDS];

typedef struct {
  char c;
} Symbol;
KHASH_MAP_INIT_STR(symtab, Symbol *)

// TODO: Support more types of literals later... I hate the non-generic part of all this
KHASH_MAP_INIT_STR(string, int)
typedef struct {
  DECLARE_VECTOR(const char *, string_val)
  khash_t(string) *string_id;
} StringPool;

StringPool *new_string_pool() {
  StringPool *ret = malloc(sizeof(StringPool));
  NEW_VECTOR(ret->string_val, sizeof(char *));
  ret->string_id = kh_init_string();
  return ret;
}

void fprint_string_pool(FILE *f, StringPool *pool) {
  fprintf(f, "string pool:\n");
  for (int i = 0; i < pool->string_val_size; i++) {
    fprintf(f, "  %d: %s\n", i, pool->string_val[i]);
  }
}

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
  StringPool *constant_pool;
} ScannerCont;

ScannerCont make_scanner_cont(FILE *in, const char *filename, StringPool *constant_pool) {
  ScannerCont cont = {
    .filename = filename,
    .pos = 0,
    .line = 0,
    .col = 0,
    .saved_pos = -1,
    .saved_line = -1,
    .saved_col = -1,
    .constant_pool = constant_pool,
  };
  DIE_IF(fseek(in, 0, SEEK_END) == -1, "seek end");
  cont.size = ftell(in);
  DIE_IF(cont.size == -1, "ftell");
  char *buf = malloc(cont.size + 2);
  DIE_IF(buf == 0, "malloc failed");
  DIE_IF(fseek(in, 0, SEEK_SET) == -1, "seek begin");
  DIE_IF(fread((char *) buf, 1, cont.size, in) < cont.size, "fread did not read enough characters");
  // add another NULL byte to enable two characters of readahead; useful for detecting comments
  buf[cont.size] = '\0';
  buf[cont.size + 1] = '\0';
  cont.buf = buf;
  return cont;
}

#define DEFINE_INTERN_GENERIC(name, storage_type) \
  int intern_##name(StringPool *pool, storage_type val) { \
    khash_t(name) *h = pool->name##_id; \
    khiter_t k = kh_get(name, h, val); \
    if (k != kh_end(h)) {  /* saw this constant before */\
      return kh_val(h, k); \
    } /* else we haven't seen the constant before */ \
    int ret; \
    k = kh_put(name, h, val, &ret); /* add the key */ \
    THROW_IF(ret == -1, EXC_SYSTEM, "kh_put failed"); \
    int id = VECTOR_SIZE(pool->name##_val); \
    APPEND_VECTOR(pool->name##_val, val); \
    kh_val(h, k) = id; \
    return id; \
  }

DEFINE_INTERN_GENERIC(string, char *)

char peek(ScannerCont *cont) {
  return cont->buf[cont->pos];
}

char peek2(ScannerCont *cont) {
  return cont->buf[cont->pos + 1];
}

char getch(ScannerCont *cont) {
  char ret = cont->buf[cont->pos++];
  if (ret == '\n') {
    cont->line++;
    cont->col = 0;
  } else {
    cont->col++;
  }
  return ret;
}

void save_pos(ScannerCont *cont) {
  cont->saved_pos = cont->pos;
  cont->saved_line = cont->line;
  cont->saved_col = cont->col;
}

/** @brief <think about how to destribute the continuation>
 *  @param ch The character you just scanned.
 *  @param cont Continuation used to call this function again with the next character. Each call will narrow the range
 *    ix_begin:ix_end and increment j by 1.
 *  @return -2 if the string built up in ch since the beginning continuation was not found, -1 if you need to call me
      again, or >= 0 if found and value is the index of cont->haystack->strings we found.
 */

int is_ident_start(int c) {
  return (c == '_') || isalpha(c);
}
int is_ident_rest(int c) {
  return (c == '_') || isalnum(c);
}

typedef enum {
  CONST_STRING,
  CONST_INTEGER,
  CONST_FLOAT,
} ConstantKind;

typedef struct {
  TokenKind kind;
  int string_id;
  union {
    int64_t int64_val;
    double double_val;
  } constant_val;
  // debugging
  const char *filename;
  int line_start;
  int line_end;
  int col_start;
  int col_end;
} Token;

Token make_partial_token(const ScannerCont *cont, TokenKind kind) {
  return (Token) {
    .kind = kind,
    .filename = cont->filename,
    .line_start = cont->saved_line,
    .line_end = cont->line,
    .col_start = cont->saved_col,
    .col_end = cont->col,
  };
}

int match_longest_prefix(ScannerCont *cont, const char **values, const int *lens, int n_values) {
  int i_prev_match = -1;
  int i = 0;
  int match = 1;
  for (int k = 0; ; k++, getch(cont)) {
    for(match = 0; i < n_values; i++) {
      if (lens[i] < k + 1) {
        continue;
      }
      if (values[i][k] == peek(cont)) {
        i_prev_match = i;
        match = 1;
        break;
      }
    }
    if (!match) {
      // was the previous match an EXACT match?
      if (i_prev_match >= 0 && lens[i_prev_match] == k) {
        return i_prev_match;
      }
      return -1;
    }
  }
  assert(0 && "Unreachable!");
}

void consume_spaces(ScannerCont *cont) {
  assert(isspace(peek(cont)));
  while (isspace(peek(cont))) {
    getch(cont);
  }
  assert(!isspace(peek(cont)));
}

char *parse_string_literal(ScannerCont *cont) {
  DECLARE_VECTOR(char, buf)
  NEW_VECTOR(buf, sizeof(char));
  char ch;
label_at_open_quote:
  assert(getch(cont) == '"');
  while (1) {
    ch = getch(cont);
    switch (ch) {
      case '\\':
        switch (peek(cont)) {
          // I could use a macro, but that gets too hairy
          case 'n': getch(cont); APPEND_VECTOR(buf, '\n'); break;
          case 'v': getch(cont); APPEND_VECTOR(buf, '\v'); break;
          case 't': getch(cont); APPEND_VECTOR(buf, '\t'); break;
          case '"': getch(cont); APPEND_VECTOR(buf, '"'); break;
          default:
            THROW(EXC_LEX_SYNTAX, "Invalid escape sequence");
        }
        break;
      case '"':
        if (isspace(peek(cont))) {
          consume_spaces(cont);
          goto label_at_open_quote;
        }
        // return is here
        APPEND_VECTOR(buf, '\0');
        return buf;
      default:
        APPEND_VECTOR(buf, ch);
        break;
    }
  }
  assert(0 && "Unreachable!");
}

int is_float_but_not_int_char(char ch) {
  return (ch == '.') || (ch == 'e') || (ch == 'E') || (ch == 'p') || (ch == 'P');
}

Token consume_next_token(ScannerCont *cont) {
  char ch;
  char *msg;
label_start:
  ch = peek(cont);
  if (ch == '\0') {
    return make_partial_token(cont, TOK_END_OF_FILE);
  }
  if (ch == '/' && peek2(cont) == '/') {
    while (getch(cont) != '\n')
      ;
    // fprintf(stderr, "exiting line comment at %d:%d\n", curr_line(cont), curr_col(cont));
    goto label_start;
  }
  if (ch == '/' && peek2(cont) == '*') {
  label_block_comment:
    while (getch(cont) != '*')
      ;
    // current char is *; what is the next one?
    if (peek(cont) == '/') {
      // make sure to consume '/' or else we would parse it as another token!
      getch(cont);
      goto label_start;  // end of comment
      // fprintf(stderr, "exiting block comment at %d:%d\n", curr_line(cont), curr_col(cont));
    }
    goto label_block_comment;  // still in comment
  }
  if (isspace(ch)) {
    consume_spaces(cont);
    goto label_start;
  }

  // Now the actual tokens begin
  save_pos(cont);
  if (ch == '"') {
    char *s = parse_string_literal(cont);
    int id = intern_string(cont->constant_pool, s);
    Token ret = make_partial_token(cont, TOK_STRING_LITERAL);
    ret.string_id = id;
    return ret;
  }
  if (
    isdigit(ch)
    || ((ch == '+' || ch == '-') && isdigit(peek2(cont)))
  ) {
    Token ret;
    const char *startptr = cont->buf + cont->pos;
    char *endptr;
    int64_t int64_val = strtoll(startptr, &endptr, 0);
    if (endptr == startptr) {
      // no digits were parsed
      goto label_throw;
    }
    // Use janky method to distinguish floats vs integers: if strtoll fails at a decimal or exponent, then we try to
    // parse as a float.
    if (is_float_but_not_int_char(*endptr)) {
      double double_val = strtod(startptr, &endptr);
      if (endptr == startptr) {
        goto label_throw;
      }
      int n_chars_read = endptr - startptr;
      for (int i = 0; i < n_chars_read; i++) {
        getch(cont);
      }
      ret = make_partial_token(cont, TOK_FLOAT_LITERAL);
      ret.constant_val.double_val = double_val;
    } else {
      int n_chars_read = endptr - startptr;
      for (int i = 0; i < n_chars_read; i++) {
        getch(cont);
      }
      ret = make_partial_token(cont, TOK_INTEGER_LITERAL);
      ret.constant_val.int64_val = int64_val;
    }
    // in either case, advance scanner
    return ret;
  label_throw:
    THROW(EXC_LEX_SYNTAX, "Expected to parse integer or double literal but nothing was parsed");
  }
  if (ispunct(ch)) {
    int i = match_longest_prefix(cont, PUNCT_VALUES, PUNCT_VALUES_len, N_PUNCTS);
    if (i >= 0) {
      return make_partial_token(cont, PUNCT_KIND(i));
    }
    // if we didn't consume anything at all, then ch is punct but doesn't match and valid token. 
    if (cont->pos == cont->saved_pos) {
      goto label_error;
    }
    // next char could be anything
    goto label_start;
  }
  if (is_ident_start(ch)) {
    // identifier_or_keyword
    int i = match_longest_prefix(cont, KEYWORD_VALUES, KEYWORD_VALUES_len, N_KEYWORDS);
    if (i >= 0) {
      return make_partial_token(cont, KEYWORD_KIND(i));
    }
    // else we are in the middle of an identifier
    while (is_ident_rest(peek(cont))) {
      getch(cont);
    }
    int span_len = cont->pos - cont->saved_pos;
    char *s = malloc(span_len + 1);
    THROW_IF(!s, EXC_SYSTEM, "malloc span_len failed");
    strlcpy(s, cont->buf + cont->saved_pos, span_len + 1);
    int id = intern_string(cont->constant_pool, s);
    Token ret = make_partial_token(cont, TOK_IDENT);
    ret.string_id = id;
    fprintf(stderr, "parsed identifier %s, id = %d\n", s, id);
    return ret;
  }
label_error:
  // Syntax error if we get here
  msg = malloc(1000);
  snprintf(msg, 1000, "Invalid character %c at position %d (line %d, col %d)", ch, cont->pos, cont->line, cont->col);
  THROW(EXC_LEX_SYNTAX, msg);
}

void fprint_string_repr(FILE *out, const char *s) {
  for (; *s != '\0'; s++) {
    switch (*s) {
      case '\n':
        s++;
        putc('\\', out);
        putc('n', out);
        break;
      case '\t':
        s++;
        putc('\\', out);
        putc('t', out);
      case '\v':
        s++;
        putc('\\', out);
        putc('v', out);
      default:
        fputc(*s, out);
        break;
    }
  }
}

void parse_start(FILE *in, const char *filename) {
  StringPool *pool = new_string_pool();
  ScannerCont cont = make_scanner_cont(in, filename, pool);
  if (setjmp(global_exception_handler) == 0) {
    while (1) {
      Token tok = consume_next_token(&cont);
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
        case TOK_STRING_LITERAL:
        case TOK_IDENT:
          fprint_string_repr(stdout, pool->string_val[tok.string_id]);
          break;
        case TOK_INTEGER_LITERAL:
          printf("%lld", tok.constant_val.int64_val);
          break;
        case TOK_FLOAT_LITERAL:
          printf("%g", tok.constant_val.double_val);
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
  fprint_string_pool(stdout, pool);
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
  #define n_PUNCT_VALUES N_PUNCTS
  fill_lens(PUNCT_VALUES);
  // hack
  #define n_KEYWORD_VALUES N_KEYWORDS
  fill_lens(KEYWORD_VALUES);

  FILE *in = fopen(argv[1], "r");
  DIE_IF(!in, "Couldn't open input file");

  parse_start(in, argv[1]);
  return 0;
}
