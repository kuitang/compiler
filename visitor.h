#pragma once
#include "common.h"
#include "tokens.h"
#include "types.h"
#include <stdio.h>

struct VisitorHeader;

typedef enum {
  PROP_IS_UNARY_EXPR,
} ParseProperty;

typedef void *(*VisitIdent)(void *visitor, const Type *type, int index, int offset);
typedef void *(*VisitFloatLiteral)(void *visitor, double double_val);
typedef void *(*VisitIntegerLiteral)(void *visitor, int64_t int64_val);
typedef void *(*VisitBinop)(void *visitor, TokenKind op, void *left, void *right);
typedef void *(*VisitAssign)(void *visitor, void *dest, void *source);
typedef void *(*Visit1)(void *visitor, void *left);
typedef void (*VisitSet)(void *result, ParseProperty prop, int val);
typedef int (*VisitGet)(void *result, ParseProperty prop);
typedef void *(*VisitDeclarator)(void *visitor, int index, int offset);
typedef void *(*VisitFunctionDefinition)(
  void *visitor,
  int function_name,
  Type *function_type
);
typedef void *(*Visit2)(void *visitor, void *left, void *right);
typedef int (*Predicate)(void *visitor, void *expr);
typedef void (*Dump)(void *visitor, FILE *out);
typedef void (*DumpResult)(FILE *out, void *expr);
typedef struct VisitorHeader *(*VisitorConstructor)(); 

// TODO: Macrofy this
typedef struct VisitorHeader {
  VisitIdent visit_ident;
  VisitFloatLiteral visit_float_literal;
  VisitIntegerLiteral visit_integer_literal;
  VisitBinop visit_binop;
  VisitAssign visit_assign;
  VisitSet set_property;
  VisitGet get_property;
  VisitDeclarator visit_declarator;
  VisitFunctionDefinition visit_function_definition;
  Dump dump;
  DumpResult dump_result;
} VisitorHeader;

// helper to install methods
#define INSTALL(v, ty, f) (v)->head.f = (ty) f;
#define INSTALL_VISITOR_METHODS(v) \
  INSTALL(v, VisitIdent, visit_ident); \
  INSTALL(v, VisitFloatLiteral, visit_float_literal); \
  INSTALL(v, VisitIntegerLiteral, visit_integer_literal); \
  INSTALL(v, VisitBinop, visit_binop); \
  INSTALL(v, VisitAssign, visit_assign); \
  INSTALL(v, VisitSet, set_property); \
  INSTALL(v, VisitGet, get_property); \
  INSTALL(v, VisitDeclarator, visit_declarator); \
  INSTALL(v, VisitFunctionDefinition, visit_function_definition); \
  INSTALL(v, Dump, dump); \
  INSTALL(v, DumpResult, dump_result); \

