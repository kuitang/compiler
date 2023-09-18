#pragma once
#include "stdint.h"
#include "tokens.h"
#include <stdio.h>

struct VisitorHeader;

typedef void *(*VisitIdent)(void *visitor, int string_id);
typedef void *(*VisitFloatLiteral)(void *visitor, double double_val);
typedef void *(*VisitIntegerLiteral)(void *visitor, int64_t int64_val);
typedef void *(*VisitBinop)(void *visitor, TokenKind op, void *left, void *right);
typedef void *(*Visit1)(void *visitor, void *left);
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
  Predicate is_unary_expr;
  Dump dump;
  DumpResult dump_result;
} VisitorHeader;

// helper to install methods
#define INSTALL(v, ty, f) (v)->head.f = (ty) f;
#define INSTALL_VISITOR_METHODS(v) \
  INSTALL(v, VisitFloatLiteral, visit_float_literal); \
  INSTALL(v, VisitIntegerLiteral, visit_integer_literal); \
  INSTALL(v, VisitBinop, visit_binop); \
  INSTALL(v, Predicate, is_unary_expr); \
  INSTALL(v, Dump, dump); \
  INSTALL(v, DumpResult, dump_result); \
