#include <stdio.h>

extern int my_func();

#define print_expr(expr) printf(#expr " = %d\n", (expr))

int main(int argc, char *argv[]) {
  print_expr(my_func(6, 2, 10, 1));
}
