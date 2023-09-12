#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../common.h"
#include <stdint.h>
#include <assert.h>
#include <ctype.h>

static char *tokens[] = {
  "return",
  "<",
  ">",
  "<=",
  "if",
  "register",
  "int",
  ">=",
  "restrict",
  "inline",
};
const int n_tokens = sizeof(tokens) / sizeof(char *);

typedef int (*compar_t)(const void *, const void *);

/*
int lex_str_cmp(const char *s1, const char *s2) {
  const int max_diff = 256;
  while (*s1++ == *s2++) {
    // equal
    if (*s1 == '\0' && *s2 == '\0') return 0;
    // s1 smaller
    if (*s1 == '\0') return -max_diff;
    // s2 smaller
    if (*s2 == '\0') return max_diff;
    // 
    
  }
}
*/

int indirect_strcmp(const void **p1, const void **p2) {
  const char *s1 = *p1;
  const char *s2 = *p2;
  return strcmp(s1, s2);
}

uint64_t prefix_code(const char *s, int len) {
  assert(len <= 8);
  uint64_t ret = 0;
  for (int i = len - 1; i >= 0; i--) {
    ret <<= 8;
    ret += s[i];
  }
  return ret;
}

void decode_prefix(char *out, uint64_t code, int len) {
  assert(len <= 8);

  uint64_t ret = 0;
  for (int i = 0; i < len; i++) {
    out[i] = code & ((1 << 8) - 1);
    code >>= 8;
  }
}

typedef struct {
  const char **strings;
  size_t *lengths;
  size_t n_strings;
} Haystack;
Haystack *new_haystack(const char **strings, size_t n_strings) {
  Haystack *s = malloc(sizeof(Haystack));
  s->strings = strings;
  DIE_IF(!s, "malloc");
  s->lengths = malloc(n_strings * sizeof(size_t));
  for (int i = 0; i < n_strings; i++) {
    s->lengths[i] = strlen(strings[i]);
    DEBUG_PRINT_EXPR("%d %lu", i, s->lengths[i]);
  }
  s->n_strings = n_strings;
  return s;
}

typedef struct {
  // static
  const char *buf;
  int size;
  const Haystack *haystack;
  // dynamic
  int pos;
} ScannerCont;

typedef struct {
  const Haystack *haystack;
  int ix_begin;
  int ix_end;
  int j;
} LongestPrefixCont;

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
  THROW_IF(cont->pos == cont->size, EXC_EOF, "EOF reached");
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

int next_longest_prefix(ScannerCont *cont) {
  DEBUG_PRINT_EXPR("new call %d", cont->pos);
  // outer loop gets the next character
  const char **strings = cont->haystack->strings;
  int n_strings = cont->haystack->n_strings;
  const size_t *lengths = cont->haystack->lengths;
  // const char *first_chars = cont->haystack->first_chars;

  int i = 0; // lower bound
  int j = n_strings - 1; // upper bound
  int k;
  char ch;
  
  DEBUG_PRINT_EXPR("%d %d", i, j);
  for (k = 0; ; k++) {
    ch = getch(cont);
    if (isspace(ch))
      break;
    // invariant: i is the FIRST string that shares a prefix and j is the LAST string that shares a prefix.
    while (i < n_strings && k < lengths[i] && strings[i][k] < ch )
      i++;
    while (j >= 0 && k < lengths[j] && strings[j][k] > ch)
      j--;
    DEBUG_PRINT_EXPR("%d %c %d %d %lu", k, ch, i, j, lengths[i]);
    /*
    if (i == j)
      // unique match
      break;
    */
    if (i == n_strings || j == 0) {
      // no match
      return -1;
    }
  }
  if (i < n_strings && k < lengths[i] && strings[i][k] == ch)
    return i;
  if (j >= 0 && k < lengths[j] && strings[j][k] == ch)
    return j;
  return -1;
}

int consume_next_token(ScannerCont *cont) {
  DEBUG_PRINT_EXPR("%d: %c", cont->pos, cont->buf[cont->pos]);
  while (isspace(getch(cont)))
    ;
  ungetch(cont);
  DEBUG_PRINT_EXPR("%d: %c", cont->pos, cont->buf[cont->pos]);
  int old_pos = cont->pos;
  int lc_ret = next_longest_prefix(cont);
  if (lc_ret >= 0) {
    printf("-------> parsed keyword [[ %s ]]\n", cont->haystack->strings[lc_ret]);
    return 1;
  }
  ungetch(cont);
  // we are in the middle of a word
  while (isalpha(getch(cont)))
    ;
  ungetch(cont);
  // now we are at the end of the word
  DEBUG_PRINT("got here!");

  // https://embeddedartistry.com/blog/2017/07/05/printf-a-limited-number-of-characters-from-a-string/
  // printf("Here are the first 5 characters: %.*s\n", 5, mystr); //5 here refers to # of characters
  int span_len = cont->pos - old_pos;
  printf("the string spanning %d:%d was < %.*s >\n", old_pos, cont->pos, cont->pos - old_pos, cont->buf + old_pos);
  return 2;
}

void emit_prefix_tree(uint64_t code, int prefix_len, const char **rest, int n_rest) {
  // base cases
  if (n_rest == 0)
    return;

  const char *head = *rest;
  if (strlen(head) == prefix_len)
    return;

  char buf[8];
  uint64_t code_head = prefix_code(head, prefix_len);
  if (code_head != code)
    return;

}

void parse_start(FILE *in, const Haystack *haystack) {
  ScannerCont cont = {
    .haystack = haystack,
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
      consume_next_token(&cont);
      DEBUG_PRINT_EXPR("%d", cont.pos);
    }
  } else {
    if (global_exception.kind == EXC_EOF)
      // expected
      return;
    PRINT_EXCEPTION();
    exit(1);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "gen_lexer: no input file\n");
    exit(1);
  }
  DEBUG_PRINT_EXPR("%lu, %lu, %d, %p", sizeof(tokens), sizeof(char *), n_tokens, tokens);
  for (int i = 0; i < n_tokens; i++) {
    printf("%p: s = %p *s = %s\n", &tokens[i], tokens[i], tokens[i]);
  }
  DEBUG_PRINT("before");
  qsort(tokens, n_tokens, sizeof(char*), (compar_t) indirect_strcmp);
  DEBUG_PRINT("after");
  for (int i = 0; i < n_tokens; i++) {
    printf("%p: %s\n", &tokens[i], tokens[i]);
  }
  // prefix code
  DEBUG_PRINT_EXPR("%llu, %llu, %llu", prefix_code("if", 2), prefix_code("inline", 2), prefix_code("int", 2));
  DEBUG_PRINT_EXPR("%llu, %llu", prefix_code("inline", 2), prefix_code("int", 2));
  DEBUG_PRINT_EXPR("%llu, %llu", prefix_code("inline", 3), prefix_code("int", 3));
  DEBUG_PRINT_EXPR("%llu, %llu, %llu", prefix_code("register", 2), prefix_code("restrict", 2), prefix_code("return", 2));

  char buf[9];

  // emit_prefix_tree(0, 0, tokens, n_tokens);

  uint64_t code1 = prefix_code("register", 2);
  decode_prefix(buf, code1, 2);

  DEBUG_PRINT_EXPR("%s", buf);

  // FILE *in = fopen("register.txt", "r");
  FILE *in = fopen(argv[1], "r");
  DIE_IF(!in, "Couldn't open input file");

  size_t *lengths = malloc(sizeof(size_t) * n_tokens);
  DIE_IF(!lengths, "malloc failed");
  for (int i = 0; i < n_tokens; i++) {
    lengths[i] = strlen(tokens[i]);
  }

  Haystack *haystack = new_haystack((const char **) tokens, n_tokens);
  parse_start(in, haystack);
  return 0;
}
