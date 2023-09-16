// this is a line comment
    // line comment in the middle of line // with // another

/* block comment
  int x = y;
  1 + 2
  char *x;
  // another // comment
  /* /* /*  */

int f1(int a) {
  return 1 + a;  // with a line comment
}

int f3() {
  char *a = "an easy string" " with another";
  char *b = "a string with \n \n\t\v \v escapes";
  char *c = "a string with \"embedded\" quotes";
  char *d = "a string"    "literal with"
    "newline whitespace" "and \" embedded quotes \""
    " and a \n\t\v escapes";
  // other escape sequences later
  char *y = "two copies";
  char *z = "two" " " "copies";
  return 17;
}

double f2(int a, double b, char *c) {
  int x = 42;
  double y = x - 12.04;
  return y - -12.30e-04 + 3E2 + a + b;
}

