#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sysexits.h>
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "common.h"

typedef struct {
  int id;
  const char *name;
  const char *literal;
} TokenDef;

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

void print_token(Token tok) {
  fprintf(
    stderr,
    "(Token) {.kind = %d, line = %d, start_col = %d, end_col = %d, value = %s}\n",
    tok.kind,
    tok.line,
    tok.start_col,
    tok.end_col,
    tok.value
  );
}

#define MAX_WORD_SIZE 32

char *safe_copy_word(const char *src) {
  size_t size = strnlen(src, MAX_WORD_SIZE) + 1;
  char *new_str = malloc(size);
  THROW_IF(!new_str, EXC_SYSTEM, "malloc error");
  strlcpy(new_str, src, size);
  return new_str;
}

typedef struct {
  FILE *in;
  // state
  char *buf;
  size_t buf_len;
  // exception handling
  // to print error messages
  int line;
  int col;
} ScannerCont;

void init_scanner_cont(ScannerCont *scont, FILE *in, char *buf, size_t buf_len) {
  scont->in = in;
  scont->buf = buf;
  scont->buf_len = buf_len;
  scont->line = 1;
  scont->col = 1;
}


Token consume_next_token(ScannerCont *scont) {
  typedef enum {
    ST_START_SPACE,
    ST_COMMENT,
    ST_NAME,
    ST_LITERAL,
  } State;
  State state = ST_START_SPACE;
  int ch, curr_line, curr_col, start_col = -1, j = 0;
  while ( (ch = getc(scont->in)) != EOF) {
    // DEBUG_PRINT_EXPR("%d", ch);
    curr_line = scont->line;
    curr_col = scont->col;
    if (ch == '\n') {
      scont->line++,
      scont->col = 1;
    } else {
      scont->col++;
    }
    switch (state) {
      case ST_START_SPACE:
        if (isspace(ch)) continue;
        if (ch == '#') {
          state = ST_COMMENT;
          continue;
        }
        // Remaining states all take a buffer, so fill the first character
        scont->buf[0] = ch;
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
        if (j > MAX_WORD_SIZE - 1) goto overflow; 
        if (isupper(ch)) {
          scont->buf[j++] = ch;
          continue;
        }
        if (isspace(ch)) {
          // space means we finished parsing.
          // make the buffer a c string for caller to use
          scont->buf[j++] = '\0';
          return (Token) {
            .kind = TOK_NAME,
            .value = scont->buf,
            .line = curr_line,
            .start_col = start_col,
            .end_col = curr_col,
          };
        }
      case ST_LITERAL:
        if (j > MAX_WORD_SIZE - 1) goto overflow; 
        if (isspace(ch)) {
          scont->buf[j++] = '\0';
          return (Token) {
            .kind = TOK_LITERAL,
            .value = scont->buf,
            .line = curr_line,
            .start_col = start_col,
            .end_col = curr_col
          };
        }
        // else assume the literal continues
        scont->buf[j++] = ch;
        continue;
    }
  }
  THROW_IF(ferror(scont->in), EXC_SYSTEM, "Input error");
  assert(feof(scont->in));
  return (Token) {
    .kind = TOK_EOF,
  };
overflow:
  scont->buf[MAX_WORD_SIZE - 1] = '\0';
  fprintf(stderr, "parse error at %d:%d: word exceeded MAX_WORD_SIZE, partial parse={%s}\n",
    scont->line, scont->col, scont->buf
  );
  exit(1);
}

typedef struct {
  TokenDef *token_defs;
  int token_defs_size;
  int token_defs_capacity;
} ParseResult;

typedef struct {
  int id;
  ParseResult *result;
  int get_last_token_again;
  Token last_token;
  ScannerCont scanner_cont;
} ParserCont;

void init_parser_cont(ParserCont *pcont, ParseResult *result, FILE *in, char *buf, size_t buf_len) {
  pcont->id = 0;
  pcont->result = result;
  pcont->get_last_token_again = 0;
  pcont->last_token = (Token) {.kind = TOK_INVALID};
  init_scanner_cont(&pcont->scanner_cont, in, buf, buf_len);
}

Token get_token(ParserCont *pcont) {
  // DEBUG_PRINT_EXPR("%d", pcont->get_last_token_again);
  if (pcont->get_last_token_again) {
    pcont->get_last_token_again = 0;
    return pcont->last_token;
  }
  Token ret = consume_next_token(&pcont->scanner_cont);
  print_token(ret);
  pcont->last_token = ret;
  return ret;
}

void unget_token(ParserCont *pcont) {
  THROW_IF(
    pcont->get_last_token_again,
    EXC_INTERNAL,
    "unget_token called but last_token was already set."
    "Attempted to unget value that was not a token"
  );
  pcont->get_last_token_again = 1;
}

/*
start = token_def*
token_def = name literal?
*/

void parse_token_def(ParserCont *pcont) {
  Token name = get_token(pcont);
  THROW_IF(name.kind != TOK_NAME, EXC_PARSE_SYNTAX, "Expected to parse TOK_NAME");

  // we parsed a token_def; it certainly has a name and we create a new id.
  pcont->id++;
  APPEND_VECTOR(
    pcont->result->token_defs,
    ((TokenDef) {.id = pcont->id, .name = safe_copy_word(name.value)})
  );

  // optional literal
  Token maybe_literal = get_token(pcont);
  if (maybe_literal.kind == TOK_LITERAL) {
    VECTOR_LAST(pcont->result->token_defs).literal = safe_copy_word(maybe_literal.value);
  } else {
    unget_token(pcont);
  }
}

#define MAX_LINE_SIZE 1024

void rewind_to_line(FILE *in, int line) {
  THROW_IF(
    fseek(in, 0, SEEK_SET) != 0,
    EXC_SYSTEM,
    "failed to rewind input file"
  );
  for (int i = 0, ch; i < line - 1; i++) {
    while ( ((ch = getc(in)) != EOF) && (ch != '\n') );
    if (ch == EOF) {
      assert(!feof(in) && "should not reach EOF because we read a limited number of lines");
      THROW_IF(ferror(in), EXC_SYSTEM, "Input error re-reading input file");
    }
    assert(ch == '\n');
  }
}

void print_syntax_error(FILE *in, int line_num, int col_start, int col_end) {
  rewind_to_line(in, line_num);
  char *line = NULL;
  size_t linecapp;
  THROW_IF(
    getline(&line, &linecapp, in) == -1,
    EXC_SYSTEM,
    "failed to read line with syntax error"
  );
  DEBUG_PRINT("after geline");
  fputs(line, stderr);
  const char *chevron = "~~~~~ ^";
  int chevron_start = col_start - strlen(chevron);
  if (chevron_start < 0) {
  // cut off the start of chevron
  chevron -= chevron_start;
  chevron_start = 0;
  } 
  fprintf(stderr, "%*s%s\n", chevron_start, "", chevron);
}

void print_offending_parse_point(const ScannerCont *scont) {
  /*
  // finish reading the line
  char *next_ch = scont-kline_buf + scont->col;
  // TODO: Off by 1 error?
  int remaining_size = MAX_LINE_SIZE - scont->col;
  if (fgets(next_ch, remaining_size, scont->in)) {
    fputs(scont->line_buf, stderr);
  }
  */
  /* 123456789ABCDEF
     this line dun FAILED
             ~~~~~ ^
             0123456
     and FAILED
     ~~~ ^
     23456  (5 - 7 = -2)
     FAILED
     ^
     6      (1 - 7 = -6)
  */
}

void parse_start(FILE *in, ParseResult *results) {
  ParserCont pcont;
  char buf[MAX_WORD_SIZE];
  init_parser_cont(&pcont, results, in, buf, MAX_WORD_SIZE);
  if (setjmp(global_exception_handler) == 0) {
    Token tok;
    while (1) {
      tok = get_token(&pcont);
      if (tok.kind == TOK_EOF) break;
      unget_token(&pcont);
      parse_token_def(&pcont);
    }
  } else {
    // handle exception here. 
    assert(global_exception.kind != EXC_UNSET && "longjmp to exception handler but exception was not set!");
    fprintf(
      stderr,
      "Exception %s at %s:%d, function %s, input position %d:%d: %s\n",
      EXCEPTION_KIND_TO_STR[global_exception.kind],
      global_exception.file,
      global_exception.line,
      global_exception.function,
      pcont.scanner_cont.line,
      pcont.scanner_cont.line,
      global_exception.message
    );
    switch (global_exception.kind) {
      case EXC_SYSTEM:
        fprintf(stderr, "In addition, the system error message was ");
        perror("");
        break;
      case EXC_INTERNAL:
        fprintf(stderr, "INTERNAL ERROR: Kui messed up.");
        break;
      case EXC_PARSE_SYNTAX:
        print_syntax_error(in, pcont.last_token.line, pcont.last_token.start_col, pcont.last_token.end_col);
        break;
      case EXC_LEX_SYNTAX:
        print_syntax_error(in, pcont.scanner_cont.line, pcont.scanner_cont.col, -1);
        break;
      default:
        assert("Unreachable!");
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "usage: gen_tokens INPUT OUTPUT\n");
    return 1;
  }
  FILE *in = fopen(argv[1], "r");
  if (!in) {
    perror("failed to open input file");
    return 1;
  }
  ParseResult result;
  NEW_VECTOR(result.token_defs, sizeof(TokenDef));
  parse_start(in, &result);
  for (int i = 0; i < result.token_defs_size; i++) {
    printf(
      "(TokenDef) {.id = %d, name = %s, literal = %s}\n",
      result.token_defs[i].id,
      result.token_defs[i].name,
      result.token_defs[i].literal
    );
  }
}
