#import <stdio.h>

extern int f1(int);

#define print_expr(expr) printf(#expr " = %d\n", (expr))

int main(int argc, char *argv[]) {
  print_expr(f1(2));
  print_expr(f1(20));
  print_expr(f1(-100));
}
