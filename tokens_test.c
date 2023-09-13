#include <stdio.h>
#include "tokens.h"

int main() {
  TokenKind k;
  k = TOK__Static_assert;
  printf("Token %d: %s\n", k, TOKEN_NAMES[k]);
  k = TOK_ASSIGN_OP;
  printf("Token %d: %s\n", k, TOKEN_NAMES[k]);
}