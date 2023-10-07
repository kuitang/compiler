#include "lexer.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <ctype.h>

#include "common.h"

// helper macros
// WARNING: Who sorts? If sorts change the order of any of the arrays above, we fail.
#define KEYWORD_KIND(ix) ((ix) + TOK_SEPARATOR_KEYWORDS + 1)
#define PUNCT_KIND(ix) ((ix) + TOK_SEPARATOR_PUNCT + 1)
#define KEYWORD_NAME(ix) TOKEN_NAMES[KEYWORD_KIND(ix)]
#define PUNCT_NAME(ix) TOKEN_NAMES[PUNCT_NAME(ix)]

#define IS_KEYWORD(kind) ((kind) > TOK_SEPARATOR_KEYWORDS) && ((kind) < TOK_SEPARATOR_PUNCT)
#define IS_PUNCT(kind) ((kind) > TOK_SEPARATOR_PUNCT)
#define KEYWORD_KIND_STR(kind) KEYWORD_NAME()

#define sizeof_string_arr_(storage) sizeof(storage) / sizeof(char *)
#define N_TOKENS sizeof_string_arr_(TOKEN_NAMES)
#define N_KEYWORDS sizeof_string_arr_(KEYWORD_VALUES)
#define N_PUNCTS sizeof_string_arr_(PUNCT_VALUES)

const char *TOKEN_NAMES[] = {
  other_tokens_(tok_string_line_)
  "TOK_SEPARATOR_KEYWORDS",
  keywords_(tok_string_line_)
  "TOK_SEPARATOR_PUNCT",
  puncts_(tok_string_line1_)
};

static const char *KEYWORD_VALUES[] = {
  keywords_(raw_string_line_)
};

static const char *PUNCT_VALUES[] = {
  puncts_(id_string_line_0_)
};

#define max(x, y) (x) > (y) ? (x) : (y)
#define fill_lens(storage) do { \
  max_##storage##_len = -1; \
  for (size_t _i = 0; _i < n_##storage; _i++) { \
    storage##_len[_i] = strlen(storage[_i]); \
    max_##storage##_len = max(storage##_len[_i], max_##storage##_len); \
  } \
} while (0)
static int PUNCT_VALUES_len[N_PUNCTS];
static int KEYWORD_VALUES_len[N_KEYWORDS];
static int max_PUNCT_VALUES_len = -1;
static int max_KEYWORD_VALUES_len = -1;

void init_lexer_module() {
  #define n_PUNCT_VALUES N_PUNCTS
  fill_lens(PUNCT_VALUES);
  // hack
  #define n_KEYWORD_VALUES N_KEYWORDS
  fill_lens(KEYWORD_VALUES);
}

StringPool *new_string_pool() {
  StringPool *ret = checked_calloc(1, sizeof(StringPool));
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

ScannerCont make_scanner_cont(FILE *in, const char *filename, StringPool *string_pool) {
  ScannerCont cont = {
    .filename = filename,
    .pos = 0,
    .line = 0,
    .col = 0,
    .saved_pos = -1,
    .saved_line = -1,
    .saved_col = -1,
    .string_pool = string_pool,
  };
  DIE_IF(fseek(in, 0, SEEK_END) == -1, "seek end");
  cont.size = ftell(in);
  DIE_IF(cont.size == -1, "ftell");
  char *buf = malloc(cont.size + 2);
  DIE_IF(buf == 0, "malloc failed");
  DIE_IF(fseek(in, 0, SEEK_SET) == -1, "seek begin");
  DIE_IF(fread((char *) buf, 1, cont.size, in) < (size_t) cont.size, "fread did not read enough characters");
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

static char peek(ScannerCont *cont) {
  return cont->buf[cont->pos];
}

static char peek2(ScannerCont *cont) {
  return cont->buf[cont->pos + 1];
}

static char getch(ScannerCont *cont) {
  char ret = cont->buf[cont->pos++];
  if (ret == '\n') {
    cont->line++;
    cont->col = 0;
  } else {
    cont->col++;
  }
  return ret;
}

static void save_pos(ScannerCont *cont) {
  cont->saved_pos = cont->pos;
  cont->saved_line = cont->line;
  cont->saved_col = cont->col;
}

static int is_ident_start(int c) {
  return (c == '_') || isalpha(c);
}
static int is_ident_rest(int c) {
  return (c == '_') || isalnum(c);
}

static Token make_partial_token(const ScannerCont *cont, TokenKind kind) {
  return (Token) {
    .kind = kind,
    .filename = cont->filename,
    .pos_start = cont->saved_pos,
    .pos_end = cont->pos,
    .line_start = cont->saved_line,
    .line_end = cont->line,
    .col_start = cont->saved_col,
    .col_end = cont->col,
  };
}

static int match_longest_prefix(ScannerCont *cont, const char **values, const int *lens, int n_values) {
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

static void consume_spaces(ScannerCont *cont) {
  assert(isspace(peek(cont)));
  while (isspace(peek(cont))) {
    getch(cont);
  }
  assert(!isspace(peek(cont)));
}

static char *parse_string_literal(ScannerCont *cont) {
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

static int is_float_but_not_int_char(char ch) {
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
    int id = intern_string(cont->string_pool, s);
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
      ret.double_val = double_val;
    } else {
      int n_chars_read = endptr - startptr;
      for (int i = 0; i < n_chars_read; i++) {
        getch(cont);
      }
      ret = make_partial_token(cont, TOK_INTEGER_LITERAL);
      ret.int64_val = int64_val;
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
    char *name = malloc(span_len + 1);
    THROW_IF(!name, EXC_SYSTEM, "malloc span_len failed");
    strlcpy(name, cont->buf + cont->saved_pos, span_len + 1);
    int id = intern_string(cont->string_pool, name);
    Token ret = make_partial_token(cont, TOK_IDENT);
    ret.string_id = id;
    ret.identifier_name = name;
    fprintf(stderr, "lexer: scanned identifier %s, id = %d\n", name, id);
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

#undef sizeof_string_arr_
#undef keywords_
#undef puncts_
#undef other_tokens_
#undef tok_enum_line_
#undef raw_string_line_
#undef tok_string_line_
#undef tok_enum_line_1_
#undef tok_string_line_1_
#undef id_string_line_0_
