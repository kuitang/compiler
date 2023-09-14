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
KHASH_MAP_INIT_INT64(integer, int)
KHASH_MAP_INIT_INT64(float, int)
typedef struct {
  DECLARE_VECTOR(const char *, string_val)
  DECLARE_VECTOR(int64_t, integer_val)
  DECLARE_VECTOR(double, float_val)
  khash_t(string) *string_id;
  khash_t(integer) *integer_id;
  khash_t(float) *float_id;
} ConstantPool;

ConstantPool *new_constant_pool() {
  ConstantPool *ret = malloc(sizeof(ConstantPool));
  NEW_VECTOR(ret->string_val, sizeof(char *));
  NEW_VECTOR(ret->integer_val, sizeof(int64_t));
  NEW_VECTOR(ret->float_val, sizeof(double));
  ret->string_id = kh_init_string();
  ret->integer_id = kh_init_integer();
  ret->float_id = kh_init_float();
  return ret;
}

void fprint_constant_pool(FILE *f, ConstantPool *pool) {
  fprintf(f, "constant pool:\n");
  fprintf(f, "  strings:\n");
  for (int i = 0; i < pool->string_val_size; i++) {
    fprintf(f, "    %d: %s\n", i, pool->string_val[i]);
  }
  fprintf(f, "  integers:\n");
  for (int i = 0; i < pool->integer_val_size; i++) {
    fprintf(f, "    %d: %lld\n", i, pool->integer_val[i]);
  }
  fprintf(f, "  floats:\n");
  for (int i = 0; i < pool->float_val_size; i++) {
    fprintf(f, "    %d: %g\n", i, pool->float_val[i]);
  }
}

typedef struct {
  // static
  const char *filename;
  const char *buf;
  int size;
  // dynamic
  int pos;
  ConstantPool *constant_pool;
  DECLARE_VECTOR(int, newlines_pos)
} ScannerCont;

ScannerCont make_scanner_cont(FILE *in, const char *filename, ConstantPool *constant_pool) {
  ScannerCont cont = {
    .filename = filename,
    .pos = 0,
    .constant_pool = constant_pool,
  };
  NEW_VECTOR(cont.newlines_pos, sizeof(int));
  DIE_IF(fseek(in, 0, SEEK_END) == -1, "seek end");
  cont.size = ftell(in);
  DIE_IF(cont.size == -1, "ftell");
  char *buf = malloc(sizeof(cont.size) + 2);
  DIE_IF(buf == 0, "malloc failed");
  DIE_IF(fseek(in, 0, SEEK_SET) == -1, "seek begin");
  DIE_IF(fread((char *) buf, 1, cont.size, in) < cont.size, "fread did not read enough characters");
  // add another NULL byte to enable two characters of readahead; useful for detecting comments
  cont.buf = buf;
  return cont;
}

// #define DEFINE_INTERN_GENERIC(name, storage_type) \
//   int intern_##name(ConstantPool *pool, storage_type val) { \
//     khash_t(name) *h = pool->name##_id; \
//     khiter_t k = kh_get(name, h, val); \
//     if (k != kh_end(h)) {  /* saw this constant before */\
//       return kh_val(h, k); \
//     } /* else we haven't seen the constant before */ \
//     int ret; \
//     k = kh_put(name, h, val, &ret); /* add the key */ \
//     THROW_IF(ret == -1, EXC_SYSTEM, "kh_put failed"); \
//     int id = VECTOR_SIZE(pool->name##_val); \
//     APPEND_VECTOR(pool->name##_val, val); \
//     kh_val(h, k) = id; \
//     return id; \
//   }
#define DEFINE_INTERN_GENERIC(name, storage_type) \
  int intern_##name(ConstantPool *pool, storage_type val) { \
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
DEFINE_INTERN_GENERIC(integer, int64_t)
DEFINE_INTERN_GENERIC(float, double)

char peek(ScannerCont *cont) {
  return cont->buf[cont->pos];
}

char peek2(ScannerCont *cont) {
  return cont->buf[cont->pos + 1];
}

int curr_line(const ScannerCont *cont) {
  return VECTOR_SIZE(cont->newlines_pos) + 1;
}

// const char *curr_ptr(const ScannerCont *cont) {
//   return cont->buf + cont->pos;
// }

int curr_col(const ScannerCont *cont) {
  int last_newline = VECTOR_SIZE(cont->newlines_pos) == 0 ? -1 : VECTOR_LAST(cont->newlines_pos);
  return cont->pos - last_newline;
}

// see https://www.cs.cmu.edu/~410/doc/doxygen.html#introduction
/** @brief Decrement position in buffer so we can get the last character again.
 *  @param cont Continuation of next_longest_prefix
 *  @return void
 */
// TODO: Handle multiple newlines...
void ungetch(ScannerCont *cont) {
  assert(cont->pos > 0 && "Haven't consumed anything yet; can't unget");
  if (peek(cont) == '\n') {
    assert(VECTOR_SIZE(cont->newlines_pos) > 0);
    POP_VECTOR_VOID(cont->newlines_pos);
  }
  cont->pos--;
  // DEBUG_PRINT_EXPR("%d:%d, last_newline = %d", curr_line(cont), curr_col(cont), VECTOR_LAST(cont->newlines_pos));  
}

char getch(ScannerCont *cont) {
  char ret = cont->buf[cont->pos];
  if (ret == '\n') {
    APPEND_VECTOR(cont->newlines_pos, cont->pos);
  }
  cont->pos++;
  // DEBUG_PRINT_EXPR("%d:%d, last_newline = %d", curr_line(cont), curr_col(cont), VECTOR_LAST(cont->newlines_pos));
  return ret;
}

void save_pos(ScannerCont *cont, int *pos, int *col) {
  *pos = cont->pos;
  *col = curr_col(cont);
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
  COST_FLOAT,
} ConstantKind;

typedef struct {
  TokenKind kind;
  ConstantKind constant_kind;
  int constant_id;
  // debugging
  const char *filename;
  int line;
  int col_start;
  int col_end;
} Token;

Token make_partial_token(const ScannerCont *cont, TokenKind kind, int col_start) {
  return (Token) {
    .kind = kind,
    .constant_kind = -1,
    .constant_id = -1,
    .filename = cont->filename,
    .line = curr_line(cont),
    .col_start = col_start,
    .col_end = curr_col(cont),
  };
}

int match_keyword_or_ident(ScannerCont *cont) {
  // DEBUG_PRINT_EXPR("new call %d", cont->pos);
  int i = 0;
  int matched = 1;
  char ch;
  for (int k = 0; matched; k++) {
    ch = getch(cont);
    // DEBUG_PRINT_EXPR("%c", ch);
    if (!is_ident_rest(ch)) {
      if (matched && KEYWORD_VALUES_len[i] == k) {
        ungetch(cont);
        return i;
      }
      ungetch(cont);
      return -1;
    }
    // advance i such that strings[i][k] == ch
    // DEBUG_PRINT_EXPR("%d %d", i, n_keywords);
    for(matched = 0; i < N_KEYWORDS; i++) {
      if (KEYWORD_VALUES_len[i] < k + 1) {
        continue;
      }
      // DEBUG_PRINT_EXPR("%d %c %d %c %s", k, ch, i, keywords[i][k], keywords[i]);
      if (KEYWORD_VALUES[i][k] == ch) {
        // DEBUG_PRINT("**************** GOT HERE ******************");
        matched = 1;
        break;
      }
    }
    // DEBUG_PRINT("Next getch...");
  }
  ungetch(cont);
  return -1;
}

// essentially a copy of above, but different exit logic
int match_punct(ScannerCont *cont) {
  // DEBUG_PRINT_EXPR("new call %d", cont->pos);
  int i = 0;
  int i_prev_match = -1;
  int matched = 1;
  char ch;
  int k;
  for (k = 0; matched; k++) {
    ch = getch(cont);
    // advance i such that strings[i][k] == ch
    // DEBUG_PRINT_EXPR("%d %d", i, n_keywords);
    for(matched = 0; i < N_PUNCTS; i++) {
      if (PUNCT_VALUES_len[i] < k + 1) {
        continue;
      }
      if (PUNCT_VALUES[i][k] == ch) {
        // DEBUG_PRINT("**************** GOT HERE ******************");
        // DEBUG_PRINT_EXPR("%d %c %d %c %s", k, ch, i, PUNCT_VALUES[i][k], PUNCT_VALUES[i]);
        i_prev_match = i;
        matched = 1;
        break;
      }
    }
    // Eventually k will be large enough that we don't match, exiting the for loop.
  }
  assert(!matched);
  if (i_prev_match >= 0) {
    // ch didn't match, but the previous k matched. Return current character and return previous match.
    ungetch(cont);
    // DEBUG_PRINT_EXPR("Returning... %d %c %d %c %s", k, ch, i, PUNCT_VALUES[i_prev_match][k], PUNCT_VALUES[i_prev_match]);
    return i_prev_match;
  }
  ungetch(cont);
  return -1;
}

void consume_spaces(ScannerCont *cont) {
  assert(isspace(peek(cont)));
  while (isspace(getch(cont)))
    ;
  ungetch(cont);
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

// Anytime we return to consume_next_token, we are not in the "middle" of any token. So any helper function that reads
// one character too many must ungetch it before returning.
Token consume_next_token(ScannerCont *cont) {
  char ch;
  char *msg;
  int pos_start = -1, col_start = -1;  // starting positions for the token we will return
label_start:
  ch = peek(cont);
  if (ch == '\0') {
    return make_partial_token(cont, TOK_END_OF_FILE, curr_col(cont));
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
  save_pos(cont, &pos_start, &col_start);
  if (ch == '"') {
    char *s = parse_string_literal(cont);
    int id = intern_string(cont->constant_pool, s);
    Token ret = make_partial_token(cont, TOK_STRING_LITERAL, col_start);
    ret.constant_id = id;
    DEBUG_PRINT_EXPR("parsed string literal %s, id = %d\n", s, id);
    return ret;
  }
  // TOOD: distinguish doubles...
  if (
    isdigit(ch)
    || ((ch == '+' || ch == '-') && isdigit(peek(cont)))
  ) {
    Token ret;
    int id;
    const char *startptr = cont->buf + cont->pos;
    char *endptr;
    int64_t int_val = strtoll(startptr, &endptr, 0);
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
      id = intern_float(cont->constant_pool, double_val);
      ret = make_partial_token(cont, TOK_FLOAT_LITERAL, col_start);
      DEBUG_PRINT_EXPR("parsed float literal %g, id = %d\n", double_val, id);
    } else {
      id = intern_integer(cont->constant_pool, int_val);
      ret = make_partial_token(cont, TOK_INTEGER_LITERAL, col_start);
      DEBUG_PRINT_EXPR("parsed integer literal %lld, id = %d\n", int_val, id);
    }
    // in either case, advance scanner
    int n_chars_read = endptr - startptr;
    for (int i = 0; i < n_chars_read; i++) {
      getch(cont);
    }
    ret.constant_id = id;
    return ret;
  label_throw:
    THROW(EXC_LEX_SYNTAX, "Expected to parse integer or double literal but nothing was parsed");
  }
  if (ispunct(ch)) {
    int i = match_punct(cont);
    if (i >= 0) {
      return make_partial_token(cont, PUNCT_KIND(i), col_start);
    }
    // if we didn't consume anything at all, then ch is punct but doesn't match and valid token. 
    if (cont->pos == pos_start) {
      goto label_error;
    }
    // next char could be anything
    goto label_start;
  }
  if (is_ident_start(ch)) {
    // identifier_or_keyword
    int i = match_keyword_or_ident(cont);
    if (i >= 0) {
      return make_partial_token(cont, KEYWORD_KIND(i), col_start);
    }
    // else we are in the middle of an identifier
    while (is_ident_rest(getch(cont)))
      ;
    ungetch(cont);
    int span_len = cont->pos - pos_start;
    char *s = malloc(span_len + 1);
    THROW_IF(!s, EXC_SYSTEM, "malloc span_len failed");
    strlcpy(s, cont->buf + pos_start, span_len + 1);
    int id = intern_string(cont->constant_pool, s);
    Token ret = make_partial_token(cont, TOK_IDENT, col_start);
    ret.constant_id = id;
    fprintf(stderr, "parsed identifier %s, id = %d\n", s, id);
    return ret;
  }
label_error:
  // Syntax error if we get here
  msg = malloc(1000);
  snprintf(msg, 1000, "Invalid character %c at position %d (line %d, col %d)", ch, cont->pos, curr_line(cont), curr_col(cont));
  THROW(EXC_LEX_SYNTAX, msg);
}

void parse_start(FILE *in, const char *filename) {
  ConstantPool *constant_pool = new_constant_pool();
  ScannerCont cont = make_scanner_cont(in, filename, constant_pool);
  if (setjmp(global_exception_handler) == 0) {
    while (1) {
      Token tok = consume_next_token(&cont);
      if (tok.kind == TOK_END_OF_FILE) {
        break;
      }
      printf("PARSED TOKEN kind=%s on line %d", TOKEN_NAMES[tok.kind], tok.line);
      switch (tok.kind) {
        case TOK_STRING_LITERAL:
        case TOK_IDENT:
          printf(", id = %d, value = %s", tok.constant_id, constant_pool->string_val[tok.constant_id]);
          break;
        case TOK_INTEGER_LITERAL:
          printf(", id = %d, value = %lld", tok.constant_id, constant_pool->integer_val[tok.constant_id]);
          break;
        case TOK_FLOAT_LITERAL:
          printf(", id = %d, value = %g", tok.constant_id, constant_pool->float_val[tok.constant_id]);
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
  fprint_constant_pool(stdout, constant_pool);
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
