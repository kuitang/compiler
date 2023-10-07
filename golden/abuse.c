#include <stdio.h>

#define M 2
#define N 4

int main() {
  int a[M][N] = {[0][1] = 1, 2, 3, [1] = 10, 11, 12};
  for (int i = 0; i < M; i++) {
    for (int j = 0; j < N; j++) {
      printf("a[%d][%d] = %d\n", i, j, a[i][j]);
    }
  }
}
