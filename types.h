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
  int size;
  int alignment;
  union {
    // for numbers
    int is_unsigned;
    // for arrays and pointers
    struct Type *base_type;
    // for structs
    struct SymbolTable *symtab;  // TODO: not a real symboltable; just a hash from names to indices.
    // for functions
    struct {
      int n_params;
      const struct Type *return_type;
      struct DeclarationSpecifiers *declaration_specifiers;
    };
  };
} Type;

typedef struct DeclarationSpecifiers {
  StorageClassSpecifier storage_class;
  Type *type;
  TypeQualifiers type_qualifiers;
} DeclarationSpecifiers;

// Type *max_type(const Type *t1, const Type *t2);
// int compare_type(const Type *t2, const Type *t2);

typedef enum {
  DC_SCALAR,
  DC_ARRAY,
  DC_FUNCTION,
} DeclaratorKind;

typedef struct Declarator {
  DeclaratorKind kind;
  int ident_string_id;  // -1 means abstract
  const char *ident_string;
  int is_pointer;
  struct Declarator *parent;
  union {
    struct {
      int n_params;
      // parallel arrays
      DeclarationSpecifiers *param_decl_specs;
      struct Declarator **param_declarators;
    };
    struct {
      int is_static;
      TypeQualifiers type_qualifiers;
      void *size_expr;
    };
  };
} Declarator;
