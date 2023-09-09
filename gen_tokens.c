#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sysexits.h>
#include <assert.h>
#include <string.h>
#include "common.h"

typedef struct {
  int id;
  const char *name;
  const char *literal;
} TokenInfo;

typedef enum {
  TOK_EOF = -1,
  TOK_INVALID = 0,
  TOK_NAME = 1,
  TOK_LITERAL,
} TokenKind;

typedef struct {
  TokenKind kind;
  int line;
  int start_col;
  int end_col;
  const char *value;
} Token;

#define MAX_WORDLEN 32

typedef struct {
  int id;
  // for debugging
  int line;
  int col;
} ScannerCont;


void init_scanner_cont(ScannerCont *cont) {
  cont->id = 0;
  cont->line = 1;
  cont->col = 1;
}


Token next_token(FILE *in, ScannerCont *cont, char *buf) {
  typedef enum {
    ST_START_SPACE,
    ST_COMMENT,
    ST_NAME,
    ST_LITERAL,
  } State;
  State state = ST_START_SPACE;
  int ch, curr_line, curr_col, start_col = -1, j = 0;
  while ( (ch = getc(in)) != EOF) {
    curr_line = cont->line;
    curr_col = cont->col;
    if (ch == '\n') {
      cont->line++,
      cont->col = 1;
    } else {
      cont->col++;
    }
    switch (state) {
      case ST_START_SPACE:
        if (isspace(ch)) continue;
        if (ch == '#') {
          state = ST_COMMENT;
          continue;
        }
        // Remaining states all take a buffer, so fill the first character
        buf[0] = ch;
        j = 1;
        if (isupper(ch)) {
          state = ST_NAME;
          start_col = curr_col;
          continue;
        }
        // exhausted all other starts, so this is a literal
        state = ST_LITERAL;
        start_col = curr_col;
        continue;
      case ST_COMMENT:
        if (ch != '\n') {
          continue;
        }
        state = ST_START_SPACE;
        continue;
      case ST_NAME:
        if (j > MAX_WORDLEN - 1) goto overflow; 
        if (isupper(ch)) {
          buf[j++] = ch;
          continue;
        }
        if (isspace(ch)) {
          // space means we finished parsing.
          // make the buffer a c string for caller to use
          buf[j++] = '\0';
          // make a new ID for the next name
          cont->id++;
          return (Token) {
            .kind = TOK_NAME,
            .value = buf,
            .line = curr_line,
            .start_col = start_col,
            .end_col = curr_col,
          };
        }
      case ST_LITERAL:
        if (j > MAX_WORDLEN - 1) goto overflow; 
        if (isspace(ch)) {
          buf[j++] = '\0';
          return (Token) {
            .kind = TOK_LITERAL,
            .value = buf,
            .line = curr_line,
            .start_col = start_col,
            .end_col = curr_col
          };
        }
        // else assume the literal continues
        buf[j++] = ch;
        continue;
    }
  }
  if (ferror(in)) {
    perror("IO error reading token_table.txt");
    exit(EX_IOERR);
  }
  assert(feof(in));
  return (Token) {
    .kind = TOK_EOF,
  };
overflow:
  buf[MAX_WORDLEN - 1] = '\0';
  fprintf(stderr, "parse error at %d:%d: word exceeded MAX_WORDLEN, partial parse={%s}\n",
    cont->line, cont->col, buf
  );
  return (Token) {
    .kind = TOK_INVALID,
  };
}

void parse_tokens(FILE *in) {
  ScannerCont cont;
  Token tok;
  init_scanner_cont(&cont);
  char buf[MAX_WORDLEN];
  do {
    tok = next_token(in, &cont, buf);
    if (tok.kind == TOK_INVALID) {
      fprintf(stderr, "Error getting next token; bailing out\n");
      return;
    }
    printf(
      "(Token) {.kind = %d, .line = %d, .start_col = %d, .end_col = %d, .value = \"%s\"}\n",
      tok.kind,
      tok.line,
      tok.start_col,
      tok.end_col,
      tok.kind > 0 ? tok.value : NULL
    );
  } while (tok.kind != TOK_EOF);
}

int main(void) {
  FILE *in = fopen("token_table.txt", "r");
  if (!in) {
    perror("Error opening token_table.txt");
    exit(EX_NOINPUT);
  }
  FILE *c_out = fopen("token.c", "w");
  FILE *h_out = fopen("token.h", "w");
  if (!(c_out && h_out)) {
    perror("Error opening output files");
    exit(EX_CANTCREAT);
  }

  parse_tokens(in);
}
