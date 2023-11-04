#pragma once
#include <stdio.h>
#include "tokens.h"
#include "types.h"

typedef struct Visitor Visitor;

typedef const Type *(*TypeOf)(const void *val);
typedef int (*TypeSize)(const Type *type);
typedef void *(*VisitFloatLiteral)(Visitor *v, double double_val);
typedef void *(*VisitIntegerLiteral)(Visitor *v, int64_t int64_val);
typedef void *(*VisitBinop)(Visitor *v, TokenKind op, void *left, void *right);
typedef void *(*VisitConditional)(Visitor *v, TokenKind op, int jump, void *left, void *right);
typedef void *(*ConvertType)(void *value, const Type *new_type);
typedef void (*VisitFunctionDefinitionStart)(
  Visitor *v,
  const char *ident
);
typedef void *(*VisitDeclaration)(
  Visitor *v,
  const Type *type,
  const char *ident
);
// TODO: const correctness
typedef void (*VisitCompoundStatement)(void *visitor);
typedef void *(*Visit0)(Visitor *v);
typedef void (*VisitVoid0)(Visitor *v);
typedef void *(*Visit1)(Visitor *v, void *left);
typedef void (*VisitVoid1)(Visitor *v, void *left);
typedef void *(*VisitArrayReference)(void *visitor, void *array, void *element, int lvalue);
typedef void *(*VisitStructReference)(void *visitor, void *left, const Member *member);
typedef int (*Predicate)(void *visitor, void *expr);
typedef Visitor *(*VisitorConstructor)(FILE *out); 
typedef void (*VisitorFinalizer)(Visitor *v);
typedef void *(*VisitAggregateReference)(void *visitor, void *object, int n_indices, const int *indices);
typedef void (*VisitAssignOffset)(void *visitor, void *aggregate, int offset, void *right);
typedef void (*EmitComment)(void *visitor, const char *fmt, ...);

// TODO: Macrofy this
// abstract type
typedef struct Visitor {
  VisitorFinalizer finalize;
  TypeOf type_of;
  TypeSize total_size;
  TypeSize align;
  TypeSize child_size;
  ConvertType convert_type;
  VisitFloatLiteral visit_float_literal;
  VisitIntegerLiteral visit_integer_literal;
  VisitBinop visit_binop;  // really just arithmetic
  VisitBinop visit_assign;
  VisitConditional visit_conditional;
  VisitDeclaration visit_declaration;
  VisitFunctionDefinitionStart visit_function_definition_start;
  VisitDeclaration visit_function_definition_param;
  VisitVoid0 visit_function_end;
  VisitVoid1 visit_return;
  VisitArrayReference visit_array_reference;
  VisitStructReference visit_struct_reference;
  VisitVoid1 visit_zero_object;
  VisitAssignOffset visit_assign_offset;
  EmitComment emit_comment;
  // Primitive types
  int pointer_size;
  Type char_type;
  Type unsigned_char_type;
  Type short_type;
  Type unsigned_short_type;
  Type int_type;
  Type unsigned_int_type;
  Type long_type;
  Type unsigned_long_type;
  Type long_long_type;
  Type unsigned_long_long_type;
  Type float_type;
  Type double_type;
  Type long_double_type;
  // child fields go here
} Visitor;

// TODO: hoist generic methods to visitor.c

// helper to install methods
#define INSTALL(v, ty, f) (v)->_visitor.f = (ty) f;
#define INSTALL_VISITOR_METHODS(v) \
  INSTALL(v, TypeOf, type_of); \
  INSTALL(v, TypeSize, total_size); \
  INSTALL(v, TypeSize, align); \
  INSTALL(v, TypeSize, child_size); \
  INSTALL(v, VisitorFinalizer, finalize); \
  INSTALL(v, ConvertType, convert_type); \
  INSTALL(v, VisitFloatLiteral, visit_float_literal); \
  INSTALL(v, VisitIntegerLiteral, visit_integer_literal); \
  INSTALL(v, VisitBinop, visit_binop); \
  INSTALL(v, VisitBinop, visit_assign); \
  INSTALL(v, VisitConditional, visit_conditional); \
  INSTALL(v, VisitDeclaration, visit_declaration); \
  INSTALL(v, VisitFunctionDefinitionStart, visit_function_definition_start); \
  INSTALL(v, VisitDeclaration, visit_function_definition_param); \
  INSTALL(v, VisitVoid0, visit_function_end); \
  INSTALL(v, VisitVoid1, visit_return); \
  INSTALL(v, VisitArrayReference, visit_array_reference); \
  INSTALL(v, VisitStructReference, visit_struct_reference); \
  INSTALL(v, VisitVoid1, visit_zero_object); \
  INSTALL(v, VisitAssignOffset, visit_assign_offset); \
  INSTALL(v, EmitComment, emit_comment); \

#define MAKE_UNSIGNED_TYPE(v, ty) v->unsigned_##ty = v->ty; v->unsigned_##ty.is_unsigned = 1

#define MAKE_ALL_UNSIGNED_TYPES(v) do { \
  MAKE_UNSIGNED_TYPE(v, char_type); \
  MAKE_UNSIGNED_TYPE(v, short_type); \
  MAKE_UNSIGNED_TYPE(v, int_type); \
  MAKE_UNSIGNED_TYPE(v, long_type); \
  MAKE_UNSIGNED_TYPE(v, long_long_type); \
} while (0)
