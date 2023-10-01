#pragma once
#include "common.h"
#include "tokens.h"
#include "types.h"
#include <stdio.h>

struct Visitor;

typedef void *(*VisitIdent)(struct Visitor *v, const Type *type, int index, int offset);
typedef void *(*VisitFloatLiteral)(struct Visitor *v, double double_val);
typedef void *(*VisitIntegerLiteral)(struct Visitor *v, int64_t int64_val);
typedef void *(*VisitBinop)(struct Visitor *v, TokenKind op, void *left, void *right);
typedef void *(*VisitConditional)(struct Visitor *v, TokenKind op, int jump, void *left, void *right);
typedef void *(*ConvertType)(void *value, const Type *new_type);
typedef void (*VisitFunctionDefinitionStart)(
  struct Visitor *v,
  DeclarationSpecifiers decl_sepcs,
  Declarator *declarator
);
typedef void *(*VisitDeclaration)(
  struct Visitor *v,
  DeclarationSpecifiers decl_sepcs,
  Declarator *declarator
);
typedef void (*VisitCompoundStatement)(void *visitor);
typedef void *(*Visit0)(struct Visitor *v);
typedef void (*VisitVoid0)(struct Visitor *v);
typedef void *(*Visit1)(struct Visitor *v, void *left);
typedef void (*VisitVoid1)(struct Visitor *v, void *left);
typedef void *(*Visit2)(void *visitor, void *left, void *right);
typedef int (*Predicate)(void *visitor, void *expr);
typedef struct Visitor *(*VisitorConstructor)(FILE *out); 
typedef void (*VisitorFinalizer)(struct Visitor *v);

// TODO: Macrofy this
// abstract type
typedef struct Visitor {
  VisitorFinalizer finalize;
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
  // child fields go here
} Visitor;

// helper to install methods
#define INSTALL(v, ty, f) (v)->_visitor.f = (ty) f;
#define INSTALL_VISITOR_METHODS(v) \
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

