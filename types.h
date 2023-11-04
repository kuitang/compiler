#pragma once

typedef enum {
  TY_ERROR = 0,
  TY_VOID,
  TY_INTEGER,
  TY_FLOAT,
  TY_ARRAY,
  TY_STRUCT,
  TY_UNION,
  TY_ENUM,
  TY_POINTER,
  TY_FUNCTION
} TypeKind;

typedef struct {
  unsigned is_const : 1;
  unsigned is_restrict : 1;
  unsigned is_volatile : 1;
} TypeQualifiers;
#define IS_QUALIFIED(q) ((q).is_const || (q).is_restrict || (q).is_volatile)

typedef struct Type Type;
typedef struct Member {
  const Type *type;
  int offset;
  const char *ident;  // for debugging
  int _ident_id;
} Member;

typedef struct Type {
  TypeKind kind;
  TypeQualifiers qualifiers;
  int size;  ///< For primitive types and pointer, number of bytes. For arrays, number of elements.
  void *size_expr;  ///< For dynamically sized arrays, codegen value object.
  int align;
  union {
    int is_unsigned;  ///< for numbers
    const Type *child_type;  ///< for arrays and pointers
    /** For structs and unions */
    struct {
      ///< If a structure or union is declared with a tag, then subsequent declarations must also have tag
      int tag;
      const void *member_map;
      int n_members;
      ///< In addition to maps, store members as an array to enable comparisons and anonymous structs and unions
      const Member **members;
    };
    /** For functions */
    struct {
      int n_params;
      const Type *return_type;
      const Type **param_types;
    };
  };
} Type;

#define IS_COMPLETE_TYPE(type) ((type)->size > 0 || (type)->size_expr)
#define IS_PRIMITIVE_TYPE(type) ((type)->kind == TY_INTEGER || (type)->kind == TY_FLOAT)
#define IS_SCALAR_TYPE(type) (IS_PRIMITIVE_TYPE(type) || (type)->kind == TY_POINTER)
#define IS_TAGGED_TYPE(type) ((type)->kind == TY_STRUCT || (type)->kind == TY_UNION || (type)->kind == TY_ENUM)

extern Type VOID_TYPE;
