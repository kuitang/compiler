#include "common.h"

const char *EXCEPTION_KIND_TO_STR[] = {
  "EXC_UNSET",
  "EXC_SYSTEM",
  "EXC_INTERNAL",
  "EXC_PARSE_SYNTAX",
  "EXC_LEX_SYNTAX",
};

// there can be only one exception
Exception global_exception;
jmp_buf global_exception_handler;

void *checked_malloc(size_t size) {
  void *ret = malloc(size);
  DIE_IF(!ret, "malloc failed");
  return ret;
}

void *checked_calloc(size_t count, size_t size) {
  void *ret = calloc(size, count);
  DIE_IF(!ret, "calloc failed");
  return ret;
}

int checked_asprintf(char **ret, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int r = vasprintf(ret, format, ap); 
  DIE_IF(r == -1, "vasprintf failed to allocate");
  return r;
}

