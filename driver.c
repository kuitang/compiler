#import <stdio.h>

extern long long f();

#define print_expr(expr) printf(#expr " = %lld\n", (expr))

int main(int argc, char *argv[]) {
  print_expr(f());
}
