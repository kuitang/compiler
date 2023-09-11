#pragma once
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

// Generic helpers
// Exceptions (there can be only one exception handler)
typedef enum {
  EXC_UNSET = 0,
  EXC_SYSTEM,
  EXC_INTERNAL,
  EXC_PARSE_SYNTAX,
  EXC_LEX_SYNTAX,
} ExceptionKind;

const char *EXCEPTION_KIND_TO_STR[] = {
  "EXC_UNSET",
  "EXC_SYSTEM",
  "EXC_INTERNAL",
  "EXC_PARSE_SYNTAX",
  "EXC_LEX_SYNTAX",
};

typedef struct {
  ExceptionKind kind;
  const char *message;
  const char *function;
  const char *file;
  int line;
} Exception;

// there can be only one exception
static Exception global_exception;
static jmp_buf global_exception_handler;

#define THROW(kind_, message_) do { \
  global_exception = (Exception) { \
    .kind = kind_, \
    .message = message_, \
    .function = __FUNCTION__, \
    .file = __FILE__, \
    .line = __LINE__ \
  }; \
  longjmp(global_exception_handler, 1); \
} while (0)
#define THROW_IF(cond, kind_, msg) if (cond) THROW(kind_, msg)

// die if no exception handler
extern int errno;
#define DIE_IF(cond, msg) do { \
  if (cond) { \
    fprintf(stderr, "Fatal error at %s:%d: %s: ", __FILE__, __LINE__, msg); \
    if (errno) perror(""); \
    exit(1); \
  } \
} while (0)

// Debugging
#define DEBUG_PRINT_EXPR(format, expr) fprintf(stderr, "DEBUG %s:%d: " #expr "=" format "\n", __FILE__, __LINE__, (expr))
#define DEBUG_PRINT(msg) fprintf(stderr, "DEBUG %s:%d: %s\n", __FILE__, __LINE__, msg)

// Vectors
// Generic vector interface: assume storage, storage ## _size, storage ## _capacity identifiers exist.
// Hence no need to typedef each usage.
#define VECTOR_INITIAL_CAPACITY 1024
#define NEW_VECTOR(storage, element_size) \
  do { \
    storage = malloc((element_size) * VECTOR_INITIAL_CAPACITY); \
    THROW_IF(!(storage), EXC_SYSTEM, "NEW_VECTOR failed"); \
    storage ## _size = 0; \
    storage ## _capacity = VECTOR_INITIAL_CAPACITY; \
  } while (0)

#define APPEND_VECTOR(storage, value) \
  do { \
    if (storage ## _size == storage ## _capacity) { \
      storage ## _capacity *= 2; \
      storage = realloc(storage, storage ## _capacity * sizeof(value)); \
      THROW_IF(!(storage), EXC_SYSTEM, "realloc failure for vector " #storage); \
    } \
    storage[(storage ## _size)++] = (value); \
  } while (0)
#define VECTOR_LAST(storage) storage[(storage ## _size) - 1]

// Basic utilities
#define MIN(x, y) (x) < (y) ? (x) : (y)
#define MAX(x, y) (x) > (y) ? (x) : (y)

