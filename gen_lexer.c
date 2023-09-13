#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../common.h"
#include <stdint.h>
#include <assert.h>
#include <ctype.h>

#define ptr_array_len(arr) sizeof(arr) / sizeof(void *)
#define max(x, y) (x) > (y) ? (x) : (y)
#define fill_lens(storage) do { \
  max_##storage##_len = -1; \
  for (int _i = 0; _i < n_##storage; _i++) { \
    storage##_len[_i] = strlen(storage[_i]); \
    max_##storage##_len = max(storage##_len[_i], max_##storage##_len); \
  } \
} while (0)
static const char *punct_tokens[] = {
  "<",
  ">",
  "<=",
  ">=",
  "=",
  ";",
};
static const int n_punct_tokens = ptr_array_len(punct_tokens);
static int max_punct_tokens_len = -1;
static int punct_tokens_len[n_punct_tokens];

static const char *keywords[] = {
  "return",
  "if",
  "register",
  "restricted",
  "int",
  "restrict",
  "inline",
};
static const int n_keywords = ptr_array_len(keywords);
static int max_keywords_len = -1;
static int keywords_len[n_keywords];

static char is_punct_token[256];
static char punct_successor[256];

typedef int (*compar_t)(const void *, const void *);

int indirect_strcmp(const void **p1, const void **p2) {
  const char *s1 = *p1;
  const char *s2 = *p2;
  return strcmp(s1, s2);
}

typedef struct {
  // static
  const char *buf;
  int size;
  // dynamic
  int pos;
} ScannerCont;

// see https://www.cs.cmu.edu/~410/doc/doxygen.html#introduction
/** @brief Decrement position in buffer so we can get the last character again.
 *  @param cont Continuation of next_longest_prefix
 *  @return void
 */
void ungetch(ScannerCont *cont) {
  assert(cont->pos > 0 && "Haven't consumed anything yet; can't unget");
  cont->pos--;
}

char peek(ScannerCont *cont) {
  return cont->buf[cont->pos];
}

char getch(ScannerCont *cont) {
  THROW_IF(cont->pos == cont->size, EXC_INTERNAL, "EOF reached");
  // DEBUG_PRINT_EXPR("before %d: %c", cont->pos, cont->buf[cont->pos]);
  char ret = cont->buf[cont->pos];
  cont->pos++;
  // DEBUG_PRINT_EXPR("after %d: %c", cont->pos, cont->buf[cont->pos]);
  return ret;
  // return cont->buf[cont->pos++];
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
  TOK_IDENTIFIER,
  TOK_PUNCT,
  TOK_KEYWORD,
  TOK_NUMBER,
  TOK_EOF,
} TokenKind;

typedef struct {
  TokenKind kind;
  const char *value;
  int len;
} Token;

int match_keyword_or_ident(ScannerCont *cont) {
  DEBUG_PRINT_EXPR("new call %d", cont->pos);
  int i = 0;
  int matched = 1;
  char ch;
  for (int k = 0; matched; k++) {
    ch = getch(cont);
    DEBUG_PRINT_EXPR("%c", ch);
    if (!is_ident_rest(ch)) {
      if (matched && keywords_len[i] == k) {
        ungetch(cont);
        return i;
      }
      return -1;
    }
    // advance i such that strings[i][k] == ch
    // DEBUG_PRINT_EXPR("%d %d", i, n_keywords);
    for(matched = 0; i < n_keywords; i++) {
      if (keywords_len[i] < k + 1) {
        continue;
      }
      // DEBUG_PRINT_EXPR("%d %c %d %c %s", k, ch, i, keywords[i][k], keywords[i]);
      if (keywords[i][k] == ch) {
        // DEBUG_PRINT("**************** GOT HERE ******************");
        matched = 1;
        break;
      }
    }
    // DEBUG_PRINT("Next getch...");
  }
  return -1;
}

Token consume_next_token(ScannerCont *cont) {
  while (isspace(getch(cont)))
    ;
  ungetch(cont);
  char ch = peek(cont);
  if (ch == '\0') {
    return (Token) {.kind = TOK_EOF, .value = 0, .len = 0};
  }
  if (is_punct_token[ch]) {
    char *buf = malloc(3);
    buf[0] = ch;
    getch(cont);
    char next_ch = peek(cont);
    if (next_ch == punct_successor[ch]) {
      getch(cont);
      // fuck memory
      buf[1] = next_ch;
      buf[2] = '\0';
      return (Token) {.kind = TOK_PUNCT, .value = buf, .len = 1};
    }
    buf[1] = '\0';
    return (Token) {.kind = TOK_PUNCT, .value = buf, .len = 2};
  }
  if (is_ident_start(ch)) {
    // identifier_or_keyword
    int start_pos = cont->pos;
    int lc_ret = match_keyword_or_ident(cont);
    if (lc_ret >= 0) {
      // found keyword
      return (Token) {.kind = TOK_KEYWORD, .value = keywords[lc_ret], .len = keywords_len[lc_ret]};
    }
    // else we are in the middle of a word
    ungetch(cont);
    // we are in the middle of a word
    while (is_ident_rest(getch(cont)))
      ;
    ungetch(cont);
    int span_len = cont->pos - start_pos;
    return (Token) {.kind = TOK_IDENTIFIER, .value = cont->buf + start_pos, .len = span_len};
  }
  if (isdigit(ch)) {
    int start_pos = cont->pos;
    while (isdigit(getch(cont)))
      ;
    ungetch(cont);
    int span_len = cont->pos - start_pos;
    return (Token) {.kind = TOK_NUMBER, .value = cont->buf + start_pos, .len = span_len};
  }
  char *msg = malloc(1000);
  snprintf(msg, 1000, "Invalid character %c at position %d", ch, cont->pos);
  THROW(EXC_LEX_SYNTAX, msg);
  assert(0 && "Unreachable state!");
}

void parse_start(FILE *in) {
  ScannerCont cont = {
    .pos = 0,
  };
  if (setjmp(global_exception_handler) == 0) {
    THROW_IF(fseek(in, 0, SEEK_END) == -1, EXC_SYSTEM);
    cont.size = ftell(in);
    THROW_IF(cont.size == -1, EXC_SYSTEM);
    cont.buf = malloc(sizeof(cont.size) + 1);
    THROW_IF(cont.buf == 0, EXC_SYSTEM, "malloc failed");
    THROW_IF(fseek(in, 0, SEEK_SET) == -1, EXC_SYSTEM);
    THROW_IF(fread((char *) cont.buf, 1, cont.size, in) < cont.size, EXC_SYSTEM);
    while (1) {
      Token tok = consume_next_token(&cont);
      if (tok.kind == TOK_EOF) {
        break;
      }
      printf("PARSED TOKEN kind=%d, value=%.*s\n", tok.kind, tok.len, tok.value);
    }
  } else {
    PRINT_EXCEPTION();
    exit(1);
  }
}

void fill_punct_tables() {
  // tokens must be lexicographically sorted
  char c1;
  for (int i = 0; i < n_punct_tokens; i++) {
    c1 = punct_tokens[i][0];
    is_punct_token[c1] = 1;
    if (punct_tokens_len[i] == 2) {
      punct_successor[c1] = punct_tokens[i][1];
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "gen_lexer: no input file\n");
    exit(1);
  }
  DEBUG_PRINT_EXPR("%d %d", n_punct_tokens, n_keywords);
  qsort(punct_tokens, n_punct_tokens, sizeof(char*), (compar_t) indirect_strcmp);
  qsort(keywords, n_keywords, sizeof(char*), (compar_t) indirect_strcmp);
  fprintf(stderr, "punctations:\n");
  for (int i = 0; i < n_punct_tokens; i++) {
    fprintf(stderr, "%8d: %s\n", i, punct_tokens[i]);
  }
  fprintf(stderr, "keywords:\n");
  for (int i = 0; i < n_keywords; i++) {
    fprintf(stderr, "%8d: %s\n", i, keywords[i]);
  }
  fill_punct_tables();
  fill_lens(punct_tokens);
  fill_lens(keywords);

  FILE *in = fopen(argv[1], "r");
  DIE_IF(!in, "Couldn't open input file");

  parse_start(in);
  return 0;
}
