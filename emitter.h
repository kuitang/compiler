#include <stdio.h>

typedef enum {
  MT_INT64,
  MT_DOUBLE
} MachineType;

typedef struct {
  FILE *out;
} EmitterCont;
