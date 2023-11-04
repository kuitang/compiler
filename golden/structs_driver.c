#include <stdio.h>
extern int f(int x, int y);

#define print_expr(expr) printf(#expr " = %d\n", (expr))
int main() {
  print_expr(f(1, 1));
  print_expr(f(2, 7));
}
