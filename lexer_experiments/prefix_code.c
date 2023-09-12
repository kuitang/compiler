#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "../common.h"

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

int main(int argc, char *argv[]) {
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
}
