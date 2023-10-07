#include <stdio.h>
extern int f1(int i);
extern int f2(int i, int j, int k, int v);

#define print_expr(expr) printf(#expr " = %d\n", (expr))
int main() {
  print_expr(f1(0));
  print_expr(f1(1));
  print_expr(f1(2));

  print_expr(f2(1, 2, 4, 0));
  print_expr(f2(1, 1, 1, 800));
  print_expr(f2(1, 2, 3, 900));
  print_expr(f2(1, 2, 1, 1000));
}
