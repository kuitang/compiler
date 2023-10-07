#pragma once

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
  TY_ERROR = 0,
  TY_VOID,
  TY_INTEGER,
  TY_FLOAT,
  TY_ARRAY,
  TY_STRUCT,
  TY_POINTER,
  TY_FUNCTION
} TypeKind;

typedef enum {
  SC_EXTERN,
  SC_STATIC,
  // auto and register and recognized but ignored 
} StorageClassSpecifier;

typedef struct {
  int is_const;
  int is_restrict;
  int is_volatile;
} TypeQualifiers;

struct SymbolTable;
typedef struct Type {
  TypeKind kind;
  // TODO: Deal with more abstractly later... also should be an ADT.
  int size;  // For primitive types and pointer, number of bytes. For arrays, number of elements.
  void *size_expr;  // UGH for arrays
  int align;
  union {
    // for numbers
    int is_unsigned;
    // for arrays and pointers
    const struct Type *child_type;
    // for structs
    struct SymbolTable *symtab;  // TODO: not a real symboltable; just a hash from names to indices.
    // for functions
    struct {
      int n_params;
      const struct Type *return_type;
      const struct Type **param_types;
    };
  };
} Type;

#define IS_SCALAR_TYPE(type) ((type)->kind == TY_INTEGER || (type)->kind == TY_FLOAT || (type)->kind == TY_POINTER)

