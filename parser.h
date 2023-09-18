#pragma once
#include <stdlib.h>
#include "common.h"
#include "lexer.h"
#include "visitor.h"

#define VEC_INITIAL_CAP 2
#define vec_t(ty) struct {ty *storage; int size; int cap;}
#define vec_push(v, x) do { \
  if ((v).size == (v).cap) { \
    (v).cap << 1; \
    (v).storage = realloc((v).storage, (v).cap * sizeof(x)); \
    DIE_IF(!(v).storage, "realloc failed"); \
  } \
  (v).storage[(v).size++] = x; \
} while (0);

#define new_vec(ty) (vec_t(ty)) { \
  .storage = check_malloc(sizeof(ty) * VEC_INITIAL_CAP), \
  .size = 0, \
  .cap = VEC_INITIAL_CAP \
}
#define vec_at(v, i) (v).storage[i]
#define vec_size(v) (v).size

typedef struct {
  ScannerCont *scont;  
  int depth;
  int pos;  // current token
  VisitorHeader *visitor;
  DECLARE_VECTOR(Token, tokens)
} ParserCont;

int parse_expression(ParserCont *cont);
