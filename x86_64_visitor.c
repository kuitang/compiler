#include <assert.h>
#include "visitor.h"
#include "common.h"

// hypothetical version for testing
typedef struct {
  char *text;
  int dest;
  int is_unary_expr;
} x86_64_Instruction;

// typedef struct {
//   const char *text;
//   int dest;
//   int src1;
//   int src2;
//   union {
//     int64_t int64_val;
//     double double_val;
//   } immediate;
//   int is_unary_expr;
// } x86_64_Instruction;

typedef struct {
  VisitorHeader head;
  int rbp_off;
  DECLARE_VECTOR(x86_64_Instruction, instructions)
} x86_64_Visitor;

static x86_64_Instruction *insert_new_instruction(x86_64_Visitor *v, int result_size) {
  APPEND_VECTOR(v->instructions, (x86_64_Instruction) {0});
  x86_64_Instruction *ret = &VECTOR_LAST(v->instructions);
  v->rbp_off -= result_size;
  ret->dest = v->rbp_off;
  return ret;
}

static void *visit_float_literal(x86_64_Visitor *v, double double_val) {
  THROWF(EXC_INTERNAL, "Floats not supported: %g", double_val);
}

static void *visit_integer_literal(x86_64_Visitor *v, int64_t int64_val) {
  x86_64_Instruction *inst = insert_new_instruction(v, 8);
  checked_asprintf(&inst->text, "\tpushq\t$%llu\t\t\t# at %%rsp = %d(%%rbp)", int64_val, inst->dest);
  return inst;
}

static char *template =
  "\tmovq\t%d(%%rbp), %%rax\n"
  "\t%s\t%d(%%rbp), %%rax\n"
  "\tpushq\t%%rax\t\t\t# at %%rsp = %d(%%rbp)\n";

static char *div_template =
  "\tmovq\t%d(%%rbp), %%rax\n"
  "\tmovq\t%%rax, %%rdx\n"
  "\tsarq\t$63, %%rdx\n"  // no cltq for 64 bits
  "\tidivq\t%d(%%rbp), %%rax\n"
  "\tpushq\t%%rax\t\t\t# at %%rsp = %d(%%rbp)\n";
  

static void *visit_binop(x86_64_Visitor *v, TokenKind op, x86_64_Instruction *left, x86_64_Instruction *right) {
  x86_64_Instruction *inst;
  switch (op) {
    case TOK_ADD_OP:
      inst = insert_new_instruction(v, 8);
      checked_asprintf(&inst->text, template, left->dest, "addq", right->dest, inst->dest);
      break;
    case TOK_SUB_OP:
      inst = insert_new_instruction(v, 8);
      checked_asprintf(&inst->text, template, left->dest, "subq", right->dest, inst->dest);
      break;
    case TOK_STAR_OP:
      inst = insert_new_instruction(v, 8);
      checked_asprintf(&inst->text, template, left->dest, "imulq", right->dest, inst->dest);
      break;
    case TOK_DIV_OP:
      inst = insert_new_instruction(v, 8);
      checked_asprintf(&inst->text, div_template, left->dest, right->dest, inst->dest);
      break;
    case TOK_COMMA:
      // special case; just return right
      return right;
    default:
      THROWF(EXC_INTERNAL, "Binop %s not supported", TOKEN_NAMES[op]);
  }
  return inst;
}

static int is_unary_expr(x86_64_Instruction *expr) {
  return 1;
}

static char *prologue = "# COMPILED BY KUI AND NOT CLANG!\n\t.globl	_f\n_f:\n\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n";
static char *epilogue = "\tleave\n\tretq\n";
static void dump(x86_64_Visitor *v, FILE *out) {
  fputs(prologue, out);
  for (int i = 0; i < v->instructions_size; i++) {
    fprintf(out, "%s\n", v->instructions[i].text);
  }
  fputs(epilogue, out);
}

static void dump_result(FILE *out, x86_64_Instruction *expr) {
  fprintf(out, "%d(%%rbp)", expr->dest);
}

VisitorHeader *new_x86_64_visitor() {
  x86_64_Visitor *v = checked_calloc(1, sizeof(x86_64_Visitor));
  NEW_VECTOR(v->instructions, sizeof(x86_64_Instruction));
  v->rbp_off = 0;
  INSTALL_VISITOR_METHODS(v)
  return (VisitorHeader *) v;
}
