#include "common.h"
#include "string.h"

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

void *checked_realloc(void *ptr, size_t size) {
  void *ret = realloc(ptr, size);
  DIE_IF(!ret, "realloc failed");
  return ret;
}

int checked_asprintf(char **ret, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int r = vasprintf(ret, format, ap); 
  DIE_IF(r == -1, "vasprintf failed to allocate");
  return r;
}

char *fmtstr(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  char *buf;
  int r = vasprintf(&buf, format, ap); 
  DIE_IF(r == -1, "vasprintf failed to allocate");
  return buf;
}

FILE *checked_fopen(const char * restrict path, const char * restrict mode) {
  FILE *ret = fopen(path, mode);
  DIE_IF(!ret, "fopen failed");
  return ret;
}

FILE *checked_fmemopen(void *restrict buf, size_t size, const char * restrict mode) {
  FILE *ret = fmemopen(buf, size, mode);
  DIE_IF(!ret, "fmemopen failed");
  return ret;
}

FILE *checked_open_memstream(char **bufp, size_t *sizep) {
  FILE *ret = open_memstream(bufp, sizep);
  DIE_IF(!ret, "open_memstream failed");
  return ret;
}

void *checked_memcpy(void *restrict dst, const void *restrict src, size_t n) {
  void *ret = memcpy(dst, src, n);
  DIE_IF(!ret, "memcpy failed");
  return ret;
}
