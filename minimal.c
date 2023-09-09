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
#define DIE_IF(cond, msg) if (cond) { fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); perror(msg); exit(errno); }
#define DIE_UNLESS(cond, msg) DIE_IF(!(cond), msg)

#define DEBUG_PRINT(format, expr) fprintf(stderr, "DEBUG %s:%d: " #expr "=" format "\n", __FILE__, __LINE__, (expr))

void next_token(FILE *infile) {
  int ch, peek;
  while ((ch = getc(infile) != EOF)) {
    DEBUG_PRINT("%d", ch);
    DEBUG_PRINT("%c", ch);
  }
}

int main(int argc, char* argv[]) {
  FILE *infile = fopen("program.c", "r");
  next_token(infile);
}
