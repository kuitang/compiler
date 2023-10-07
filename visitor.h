#pragma once
#include "tokens.h"
#include "types.h"
#include <stdio.h>

struct Visitor;

typedef const Type *(*TypeOf)(const struct Type *type);
typedef int (*TypeSize)(const struct Type *type);
typedef void *(*VisitFloatLiteral)(struct Visitor *v, double double_val);
typedef void *(*VisitIntegerLiteral)(struct Visitor *v, int64_t int64_val);
typedef void *(*VisitBinop)(struct Visitor *v, TokenKind op, void *left, void *right);
typedef void *(*VisitConditional)(struct Visitor *v, TokenKind op, int jump, void *left, void *right);
typedef void *(*ConvertType)(void *value, const struct Type *new_type);
typedef void (*VisitFunctionDefinitionStart)(
  struct Visitor *v,
  const char *ident
);
typedef void *(*VisitDeclaration)(
  struct Visitor *v,
  const struct Type *type,
  const char *ident
);
typedef void (*VisitCompoundStatement)(void *visitor);
typedef void *(*Visit0)(struct Visitor *v);
typedef void (*VisitVoid0)(struct Visitor *v);
typedef void *(*Visit1)(struct Visitor *v, void *left);
typedef void (*VisitVoid1)(struct Visitor *v, void *left);
typedef void *(*VisitArrayReference)(void *visitor, void *array, void *element, int lvalue);
typedef int (*Predicate)(void *visitor, void *expr);
typedef struct Visitor *(*VisitorConstructor)(FILE *out); 
typedef void (*VisitorFinalizer)(struct Visitor *v);
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
  VisitVoid1 visit_zero_object;
  VisitAssignOffset visit_assign_offset;
  EmitComment emit_comment;
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
  INSTALL(v, VisitVoid1, visit_zero_object); \
  INSTALL(v, VisitAssignOffset, visit_assign_offset); \
  INSTALL(v, EmitComment, emit_comment); \
