#include <stdio.h>
int main(int argc, char* argv[]) {
  int ch;
  FILE *f = fopen("program.c", "r");
  for (int i = 0; i < 10; i++) {
    ch = getc(f);
    printf("Read %d = %c\n", ch, ch);
  }
}
