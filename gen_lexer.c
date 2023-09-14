#include "tokens.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <ctype.h>
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
  char ret = cont->buf[cont->pos];
  cont->pos++;
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
  DEBUG_PRINT_EXPR("new call %d", cont->pos);
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
        DEBUG_PRINT("**************** GOT HERE ******************");
        DEBUG_PRINT_EXPR("%d %c %d %c %s", k, ch, i, PUNCT_VALUES[i][k], PUNCT_VALUES[i]);
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
    DEBUG_PRINT_EXPR("Returning... %d %c %d %c %s", k, ch, i, PUNCT_VALUES[i_prev_match][k], PUNCT_VALUES[i_prev_match]);
    return i_prev_match;
  }
  ungetch(cont);
  return -1;
}

// Anytime we return to consume_next_token, we are not in the "middle" of any token. So any helper function that reads
// one character too many must ungetch it before returning.
Token consume_next_token(ScannerCont *cont) {
  char ch;
  char *msg;
label_start:
  ch = peek(cont);
  if (ch == '\0') {
    return (Token) {.kind = TOK_END_OF_FILE, .value = 0, .len = 0};
  }
  if (isspace(ch)) {
    while (isspace(getch(cont)))
      ;
    ungetch(cont);
    assert(!isspace(peek(cont)));
    goto label_start;
  }
  if (ispunct(ch)) {
    int start_pos = cont->pos;
    int i = match_punct(cont);
    if (i >= 0) {
      return (Token) {.kind = PUNCT_KIND(i), .value = PUNCT_VALUES[i], .len = PUNCT_VALUES_len[i]};
    }
    // if we didn't consume anything at all, then ch is punct but doesn't match and valid token. 
    if (cont->pos == start_pos)
      goto label_error;
    // next char could be anything
    goto label_start;
  }
  if (is_ident_start(ch)) {
    // identifier_or_keyword
    int start_pos = cont->pos;
    int i = match_keyword_or_ident(cont);
    if (i >= 0) {
      // found keyword
      return (Token) {.kind = KEYWORD_KIND(i), .value = KEYWORD_VALUES[i], .len = KEYWORD_VALUES_len[i]};
    }
    // else we are in the middle of an identifier
    while (is_ident_rest(getch(cont)))
      ;
    ungetch(cont);
    int span_len = cont->pos - start_pos;
    return (Token) {.kind = TOK_IDENT, .value = cont->buf + start_pos, .len = span_len};
  }
  if (isdigit(ch)) {
    int start_pos = cont->pos;
    while (isdigit(getch(cont)))
      ;
    ungetch(cont);
    int span_len = cont->pos - start_pos;
    return (Token) {.kind = TOK_INTEGER_LITERAL, .value = cont->buf + start_pos, .len = span_len};
  }
label_error:
  // Syntax error if we get here
  msg = malloc(1000);
  snprintf(msg, 1000, "Invalid character %c at position %d", ch, cont->pos);
  THROW(EXC_LEX_SYNTAX, msg);
}

void parse_start(FILE *in) {
  ScannerCont cont = {
    .pos = 0,
  };
  if (setjmp(global_exception_handler) == 0) {
    THROW_IF(fseek(in, 0, SEEK_END) == -1, EXC_SYSTEM, "seek end");
    cont.size = ftell(in);
    THROW_IF(cont.size == -1, EXC_SYSTEM, "ftell");
    cont.buf = malloc(sizeof(cont.size) + 1);
    THROW_IF(cont.buf == 0, EXC_SYSTEM, "malloc failed");
    THROW_IF(fseek(in, 0, SEEK_SET) == -1, EXC_SYSTEM, "seek begin");
    THROW_IF(fread((char *) cont.buf, 1, cont.size, in) < cont.size, EXC_SYSTEM, "fread did not read enough characters");
    while (1) {
      Token tok = consume_next_token(&cont);
      if (tok.kind == TOK_END_OF_FILE) {
        break;
      }
      printf("PARSED TOKEN kind=%s, value=%.*s\n", TOKEN_NAMES[tok.kind], tok.len, tok.value);
    }
  } else {
    PRINT_EXCEPTION();
    exit(1);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "gen_lexer: no input file\n");
    exit(1);
  }
  printf("punctations:\n");
  for (int i = 0; i < N_PUNCTS; i++) {
    printf("%8d: %s\n", i, PUNCT_VALUES[i]);
  }
  printf("keywords:\n");
  for (int i = 0; i < N_KEYWORDS; i++) {
    printf("%8d: %s\n", i, KEYWORD_VALUES[i]);
  }
  #define n_PUNCT_VALUES N_PUNCTS
  fill_lens(PUNCT_VALUES);
  // hack
  #define n_KEYWORD_VALUES N_KEYWORDS
  fill_lens(KEYWORD_VALUES);

  FILE *in = fopen(argv[1], "r");
  DIE_IF(!in, "Couldn't open input file");

  parse_start(in);
  return 0;
}
