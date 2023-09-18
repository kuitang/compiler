#include <assert.h>
#include "visitor.h"
#include "common.h"
#include <stdint.h>

// hypothetical version for testing

typedef struct AstNode {
  TokenKind kind;
  union {
    int64_t int64_val;
    double float64_val;
  } immediate;
  struct AstNode *left;
  struct AstNode *right;
} AstNode;

typedef struct {
  VisitorHeader head;
} AstVisitor;

static AstNode *new_node(TokenKind kind) {
  AstNode *ret = checked_calloc(1, sizeof(AstNode));
  ret->kind = kind;
  return ret;
}

static void *visit_float_literal(AstVisitor *v, double float64_val) {
  AstNode *ret = new_node(TOK_FLOAT_LITERAL);
  ret->immediate.float64_val = float64_val;
  return ret;
}

static void *visit_integer_literal(AstVisitor *v, int64_t int64_val) {
  AstNode *ret = new_node(TOK_INTEGER_LITERAL);
  ret->immediate.int64_val = int64_val;
  return ret;
}

static void *visit_binop(AstVisitor *v, TokenKind op, AstNode *left, AstNode *right) {
  AstNode *ret = new_node(op);
  ret->left = left;
  ret->right = right;
  return ret;
}

static int is_unary_expr(AstNode *expr) {
  return 1;
}

static void dump(AstVisitor *v, FILE *out) {
  // nothing to print; can only print the final result
  return;
}

// fprintf(stderr, "%*s%s\n", chevron_start, "", chevron);
static void dump_result_recur(FILE *out, AstNode *expr, int depth) {
  if (!expr)
    return;
  fprintf(out, "%*s%s:\n", 2 * depth, "", TOKEN_NAMES[expr->kind]);
  depth++;
  if (expr->kind == TOK_FLOAT_LITERAL) {
    fprintf(out, "%*simmediate F64: %g\n", 2 * depth, "", expr->immediate.float64_val);
  } else if (expr->kind == TOK_INTEGER_LITERAL) {
    fprintf(out, "%*simmediate I64: %lld\n", 2 * depth, "", expr->immediate.int64_val);
  } else {
    fprintf(out, "%*s%s:\n", 2 * depth, "", "left");
    dump_result_recur(out, expr->left, depth + 1);
    fprintf(out, "%*s%s:\n", 2 * depth, "", "right");
    dump_result_recur(out, expr->right, depth + 1);
  }
}

static void dump_result(FILE *out, AstNode *expr) {
  dump_result_recur(out, expr, 0);
}

// TODO: graphiz output eventually
VisitorHeader *new_ast_visitor() {
  AstVisitor *v = checked_calloc(1, sizeof(AstVisitor));
  INSTALL_VISITOR_METHODS(v)
  return (VisitorHeader *) v;
}
