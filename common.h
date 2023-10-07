#pragma once
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

// Generic helpers
// Exceptions (there can be only one exception handler; make a stack later)
typedef enum {
  EXC_UNSET = 0,
  EXC_SYSTEM,
  EXC_INTERNAL,
  EXC_PARSE_SYNTAX,
  EXC_LEX_SYNTAX,
  EXC_EMITTER,
} ExceptionKind;

// TODO: Refactor the whole thing later

#define THROW(kind_, message_) do { \
  global_exception = (Exception) { \
    .kind = kind_, \
    .message = message_, \
    .function = __FUNCTION__, \
    .file = __FILE__, \
    .line = __LINE__ \
  }; \
  if (kind_ == EXC_INTERNAL) { \
    PRINT_EXCEPTION(); \
    fprintf(stderr, "INTERNAL ERROR -- trapping to debugger!\n"); \
    raise(SIGTRAP); \
  } \
  longjmp(global_exception_handler, 1); \
} while (0)

#define THROWF(kind_, format, ...) do { \
  char *message; \
  asprintf(&message, format, __VA_ARGS__); \
  THROW(kind_, message); \
} while (0);

#define THROW_IF(cond, kind_, ...) if (cond) THROW(kind_, #cond ": " __VA_ARGS__)
#define THROWF_IF(cond, kind_, format, ...) if (cond) THROWF(kind_, format, __VA_ARGS__)

#define PRINT_EXCEPTION() do { \
  fprintf( \
    stderr, \
    "Exception %s at %s:%d, function %s: %s\n", \
    EXCEPTION_KIND_TO_STR[global_exception.kind], \
    global_exception.file, \
    global_exception.line, \
    global_exception.function, \
    global_exception.message \
  ); \
} while (0)

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
#define DEBUG_PRINT_EXPR(format, ...) fprintf(stderr, "DEBUG %s in %s:%d: " #__VA_ARGS__ " = " format "\n", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_PRINT(msg) fprintf(stderr, "DEBUG %s:%d: %s\n", __FILE__, __LINE__, msg)

// Checked functions
void *checked_malloc(size_t size);
void *checked_calloc(size_t count, size_t size);
int checked_asprintf(char **ret, const char *format, ...);
void *checked_realloc(void *ptr, size_t size);
void *checked_memcpy(void *restrict dst, const void *restrict src, size_t n);

FILE *checked_fopen(const char * restrict path, const char * restrict mode);
FILE *checked_open_memstream(char **bufp, size_t *sizep);
FILE *checked_fmemopen(void *restrict buf, size_t size, const char * restrict mode);
#define checked_fclose(stream) DIE_IF(fclose(stream), "fclose failed")

char *fmtstr(const char *format, ...);

// Vectors
// Generic vector interface: assume storage, storage ## _size, storage ## _capacity identifiers exist.
// Hence no need to typedef each usage.
#define VECTOR_INITIAL_CAPACITY 1024
#define DECLARE_VECTOR(type, storage) \
  type *storage; \
  int storage##_size; \
  int storage##_capacity;

#define NEW_VECTOR(storage, element_size) \
  do { \
    storage = checked_calloc(VECTOR_INITIAL_CAPACITY, element_size); \
    storage ## _size = 0; \
    storage ## _capacity = VECTOR_INITIAL_CAPACITY; \
  } while (0)

#define APPEND_VECTOR(storage, value) \
  do { \
    if (storage ## _size == storage ## _capacity) { \
      storage ## _capacity *= 2; \
      storage = checked_realloc(storage, storage ## _capacity * sizeof(value)); \
    } \
    storage[(storage ## _size)++] = (value); \
  } while (0)
#define VECTOR_LAST(storage) storage[(storage ## _size) - 1]
#define VECTOR_SIZE(storage) storage ## _size
#define POP_VECTOR(storage) storage[(storage ## _size)--]
#define POP_VECTOR_VOID(storage) (storage ## _size)--

// Basic utilities
#define MIN(x, y) (x) < (y) ? (x) : (y)
#define MAX(x, y) (x) > (y) ? (x) : (y)

typedef struct {
  ExceptionKind kind;
  const char *message;
  const char *function;
  const char *file;
  int line;
} Exception;

// better version
#define Vec(type) struct {void *storage; int element_size; int size; int capacity;}
#define new_vec(type) { \
  .storage = checked_malloc(VECTOR_INITIAL_CAPACITY * sizeof(type)), \
  .element_size = sizeof(type), \
  .size = 0, \
  .capacity = VECTOR_INITIAL_CAPACITY \
}
#define vec_push(type, vec, val) do {\
  if ((vec).size == (vec).capacity) { \
    (vec).capacity << 1; \
    vec = checked_realloc((vec).capacity * sizeof(type)); \
  } \
  (vec)[(vec).size++] = (val) \
} while(0)

#define vec_peek(vec) (vec)->storage[(vec).size]
#define vec_popv(vec) (vec).size--
#define vec_size(vec) (vec).size
#define vec_pop(vec) (vec).storage[(vec).size--]
#define vec_ix(vec, ix) (vec).storage[ix * sizeof(type)]
  

extern const char *EXCEPTION_KIND_TO_STR[];
extern Exception global_exception;
extern jmp_buf global_exception_handler;

