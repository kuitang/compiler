#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <ctype.h>

// Generic helpers
// To be used only when errno is set
#define DIE_IF(cond, msg) if (cond) { fprintf(stderr, "%s:%d: fatal error: ", __FILE__, __LINE__); perror(msg); exit(errno); }
#define DIE_UNLESS(cond, msg) DIE_IF(!(cond), msg)

// Debugging
#define DEBUG_PRINT(format, expr) fprintf(stderr, "DEBUG %s:%d: " #expr "=" format "\n", __FILE__, __LINE__, (expr))

// Dynamic arrays
#define MAX_WORDLEN 256
#define DYNAMIC_ARRAY_INIT_CAPACITY 1024
#define APPEND_DYNAMIC_ARRAY(storage, size, capacity, value) \
  do { \
    if (size == capacity) { \
      capacity *= 2; \
      storage = realloc(storage, capacity * sizeof(value)); \
      DIE_UNLESS(storage, "realloc failure for dynamic array " #storage); \
    } \
    storage[size++] = value; \
  } while (0)

// Application specific types
typedef struct {
  FILE *infile;
  FILE *outfile;
  // Parsing and debugging
  int line;
  int col;
  // To replace with a hashmap later
  char** strings;
  int n_strings;
  int strings_capacity;
} CompilerContext;

// TODO: Get a hashmap later
typedef struct {
  int n_vars;
  int stack_sz;
  char **variable_names;
  int *variable_szs;
} LocalVars;

typedef struct {
  char **outbuf;
  LocalVars local_vars;
  _Bool is_leaf;
} FunctionContext;


// goddamned those tables without dicts...
typedef enum {
  TOK_INVALID = 0,
  TOK_EOF,
  // Terminals
  tok_begin_terminals_,
  TOK_SEMICOLON,
  TOK_LT_PAREN,
  TOK_RT_PAREN,
  TOK_LT_BRACE,
  TOK_RT_BRACE,
  TOK_ASSIGN,
  TOK_EQ,
  TOK_IF,
  TOK_RETURN,
  TOK_INT,

  tok_end_terminals_,

  TOK_NUM_LITERAL,

  // strings
  TOK_IDENTIFIER,
  TOK_STRING,
} TokenKind;

typedef struct {
  TokenKind kind;
  int value;
} Token;

static const char* TOKEN_TERMINALS[] = {
  ";",
  "(",
  ")",
  "{",
  "}",
  "=",
  "==",
  "if",
  "return",
  "int"
};


CompilerContext *new_compiler_context(FILE *infile, FILE *outfile) {
  CompilerContext *ctx = malloc(sizeof(CompilerContext));
  ctx->infile = infile;
  ctx->outfile = outfile;
  ctx->line = 0;
  ctx->col = 0;
  ctx->strings = malloc(DYNAMIC_ARRAY_INIT_CAPACITY * sizeof(void*));
  DIE_UNLESS(ctx->strings, "malloc ctx->idenfiers failed");
  ctx->n_strings = 0;
  ctx->strings_capacity = DYNAMIC_ARRAY_INIT_CAPACITY;
  return ctx;
}

void free_compiler_context(CompilerContext *ctx) {
  fclose(ctx->infile);
  fclose(ctx->outfile);
  for (int i = 0; i < ctx->n_strings; i++) {
    free(ctx->strings[i]);
  }
  free(ctx->strings);
}

int intern_string(CompilerContext *ctx, const char *string) {
  for (int i = 0; i < ctx->n_strings; i++) {
    if (strncmp(string, ctx->strings[i], MAX_WORDLEN) == 0) {
      return i;
    }
  }
  // not found: allocate a new string and add it to ctx
  char *interned_string = malloc(strlen(string) + 1);
  DIE_UNLESS(interned_string, "malloc failure for interned_string");
  APPEND_DYNAMIC_ARRAY(
    ctx->strings, ctx->n_strings, ctx->strings_capacity,
    interned_string
  );
  return ctx->n_strings;
}

void print_token(const CompilerContext *ctx, Token tok) {
  DEBUG_PRINT("%d", tok.kind);
  if (tok.kind > tok_begin_terminals_ && tok.kind < tok_end_terminals_) {
    fprintf(stderr, "%s", TOKEN_TERMINALS[tok.kind - tok_begin_terminals_ - 1]);
  } else {
    switch (tok.kind) {
      case TOK_INT:
        fprintf(ctx->outfile, "int");
      case TOK_NUM_LITERAL:
        fprintf(ctx->outfile, "%d", tok.value);
        break;
      case TOK_IDENTIFIER:
        fprintf(ctx->outfile, "%s", ctx->strings[tok.value]);  // no quotes for string
        break;
      case TOK_STRING:
        fprintf(ctx->outfile, "\"%s\"",ctx->strings[tok.value]);  // no quotes for string
        break;
      default:
        DEBUG_PRINT("%d", tok.kind);
        break;
    }
  }
  fprintf(ctx->outfile, " ");
}

void scan_word(CompilerContext *ctx, char *buf, int first) {
  assert(isalpha(first) && "first character must be alpha");
  int ch, i;
  buf[0] = first;
  for (
    i = 1;
    (i < MAX_WORDLEN - 1)
      && ((ch = getc(ctx->infile)) != EOF)
      && (isalnum(ch) || (ch == '_'));
    i++
  ) {
    buf[i] = ch;
    DEBUG_PRINT("%d", i);
    DEBUG_PRINT("%c", buf[i]);
  }
  if (feof(ctx->infile) || ferror(ctx->infile)) {
    fprintf(
      stderr,
      "Unexpected EOF or error while scanning string, partial scan=%s, line=%d, col=%d\n",
      buf, ctx->line, ctx->col
    );
    exit(1);
  }
  buf[i] = '\0';
  DEBUG_PRINT("%s", buf);
  DEBUG_PRINT("%lu", strlen(buf));
}

static const char *KEYWORD_STRINGS[] = {
  "if",
  "return",
  "int",
};

static TokenKind KEYWORD_VALUES[] = {
  TOK_IF,
  TOK_RETURN,
  TOK_INT,
};

#define N_KEYWORDS 3

int try_scan_keyword(Token *tokptr, const char *str) {
  for (int i = 0; i < N_KEYWORDS; i++) {
    DEBUG_PRINT("%s", str);
    DEBUG_PRINT("%s", KEYWORD_STRINGS[i]);
    DEBUG_PRINT("%lu", strlen(str));
    DEBUG_PRINT("%lu", strlen(KEYWORD_STRINGS[i]));
    DEBUG_PRINT("%d", strncmp(str, KEYWORD_STRINGS[i], MAX_WORDLEN));
    if (strncmp(str, KEYWORD_STRINGS[i], MAX_WORDLEN) == 0) {
      tokptr->kind = KEYWORD_VALUES[i];
      return 1;
    }
  }
  return 0;
}


Token next_token(CompilerContext *ctx) {
  Token tok;
  int ch, peek;
  char buf[MAX_WORDLEN];
  while ((ch = getc(ctx->infile)) != EOF) {
    ctx->col++;
    if (isspace(ch)) {
      if (ch == '\n') {
        ctx->line++;
      }
      continue;
    }
    switch (ch) {
      case '(':
        tok.kind = TOK_LT_PAREN; return tok;
      case ')':
        tok.kind = TOK_RT_PAREN; return tok;
      case '{':
        tok.kind = TOK_LT_BRACE; return tok;
      case '}':
        tok.kind = TOK_RT_BRACE; return tok;
      case ';':
        tok.kind = TOK_SEMICOLON; return tok;
      case '=':
        // See if next character is also =
        peek = getc(ctx->infile);
        if (peek < 0) goto read_err;
        if (peek == '=') {
          // it was
          ctx->col++;
          tok.kind = TOK_EQ;
        } else {
          // it wasn't...
          if (ungetc(peek, ctx->infile) == EOF) goto read_err;
          ctx->col--;
          tok.kind = TOK_ASSIGN;
        }
        return tok;
      default:
        // First, try to scan a word, i.e. valid identifier, which is either a keyword or identifier.
        if (isalpha(ch)) {
          scan_word(ctx, buf, ch);
          // is it a keyword?
          if (try_scan_keyword(&tok, buf)) {
            DEBUG_PRINT("%d", tok.kind);
            return tok;
          } else {
            // then it must be an identifier
            tok.kind = TOK_IDENTIFIER;
            tok.value = intern_string(ctx, buf);
            return tok;
            // DEBUG_PRINT("%s", ctx->strings[tok.value]);
          }
        } else if (isnumber(ch)) {
          // If not a string, then a number
          goto unreachable;
        }
    }
  }
read_err:
  if (ferror(ctx->infile)) {
    fprintf(stderr, "Failed to read input, line=%d, col=%d\n", ctx->line, ctx->col);
    exit(1);
  }
  if (feof(ctx->infile)) {
    tok.kind = TOK_EOF;
    return tok;
  }
unreachable:
  fprintf(stderr, "Unexpected character: %c (%d), line=%d, col=%d\n", ch, ch, ctx->line, ctx->col);
  fclose(ctx->outfile);
  exit(1);
}


void compile_program(CompilerContext *ctx) {
  Token tok = next_token(ctx);
  while (tok.kind != TOK_EOF) {
    print_token(ctx, tok);
    tok = next_token(ctx);
  }
}


int main(int argc, char* argv[]) {
  CompilerContext *ctx = new_compiler_context(stdin, stdout);
  compile_program(ctx);
  free_compiler_context(ctx);
  return 0;
}
