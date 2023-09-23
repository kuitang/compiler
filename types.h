#pragma once
#include "vendor/klib/khash.h"

typedef struct {
  int char_size;
  int short_size;
  int int_size;
  int long_size;
  int long_long_size;
  int pointer_size;
  int float_size;
  int double_size;
  int long_double_size;
} MachineSizes;

struct Type;

typedef enum {
  TY_VOID,
  TY_INTEGER,
  TY_FLOAT,
  TY_ARRAY,
  TY_STRUCT,
  TY_POINTER,
  TY_FUNCTION
} TypeKind;

typedef struct {
  int index;
  struct Type *type;
} TypeField;

typedef struct DeclarationSpecifiers {
  const struct Type *type;
  int some_flag;
} DeclarationSpecifiers;

struct SymbolTable;
typedef struct Type {
  TypeKind kind;
  int size;
  int alignment;
  union {
    // for numbers
    int is_unsigned;
    // for arrays and pointers
    struct Type *base_type;
    // for structs
    struct SymbolTable *symtab;
    // for functions
    struct {
      int n_params;
      const struct Type *return_type;
      DeclarationSpecifiers *declaration_specifiers;
    };
  };
} Type;

