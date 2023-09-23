#include <assert.h>
#include "visitor.h"
#include "common.h"

// hypothetical version for testing
typedef struct {
  char *text;
  int dest;
  int is_unary_expr;
} SsaInstruction;

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
// } SsaInstruction;

typedef struct {
  VisitorHeader head;
  DECLARE_VECTOR(SsaInstruction, instructions)
} SsaVisitor;

static SsaInstruction *new_instruction(SsaVisitor *v) {
  APPEND_VECTOR(v->instructions, (SsaInstruction) {0});
  SsaInstruction *ret = &VECTOR_LAST(v->instructions);
  int dest = v->instructions_size;
  ret->dest = dest;
  return ret;
}

static void set_property(SsaInstruction *result, ParseProperty prop, int val) {
  switch (prop) {
    case PROP_IS_UNARY_EXPR:
      result->is_unary_expr = val;
      break;
    default:
      assert(0 && "unsupported property");
  }
}

static int get_property(SsaInstruction *result, ParseProperty prop) {
  switch (prop) {
    case PROP_IS_UNARY_EXPR:
      return result->is_unary_expr;
    default:
      assert(0 && "unsupported property");
  }
  
}
static void *visit_ident(SsaVisitor *v, const Type *type, int index, int offset) {
  SsaInstruction *inst = new_instruction(v);
  checked_asprintf(&inst->text, "t%d = address of local #%d (= (stack-%d) with size %d)", inst->dest, index, offset, type->size);
  return inst;
}

static void *visit_float_literal(SsaVisitor *v, double double_val) {
  SsaInstruction *inst = new_instruction(v);
  checked_asprintf(&inst->text, "t%d = load immediate F64 $%g", inst->dest, double_val);
  return inst;
}

static void *visit_integer_literal(SsaVisitor *v, int64_t int64_val) {
  SsaInstruction *inst = new_instruction(v);
  checked_asprintf(&inst->text, "t%d = load immediate I64 $%llu", inst->dest, int64_val);
  return inst;
}

// For actual SSA, you need to realize every local variable so you can reassign. This thing is not SSA.
static void *visit_assign(SsaVisitor *v, SsaInstruction *dest, SsaInstruction *source) {
  SsaInstruction *inst = new_instruction(v);
  checked_asprintf(
    &inst->text,
    "move content of t%d to t%d\nt%d = t%d",
    source->dest,
    dest->dest,
    inst->dest,
    dest->dest
  );
  return inst;
}

static void *visit_binop(SsaVisitor *v, TokenKind op, SsaInstruction *left, SsaInstruction *right) {
  char *ssa_op;
  switch (op) {
    case TOK_ADD_OP:
      ssa_op = "add";
      break;
    case TOK_SUB_OP:
      ssa_op = "sub";
      break;
    case TOK_STAR_OP:
      ssa_op = "mul";
      break;
    case TOK_DIV_OP:
      ssa_op = "div";
      break;
    case TOK_COMMA:
      // special case; just return right
      return right;
    default:
      THROWF(EXC_INTERNAL, "Binop %s not supported", TOKEN_NAMES[op]);
  }
  SsaInstruction *inst = new_instruction(v);
  checked_asprintf(&inst->text, "t%d = %s t%d, t%d", inst->dest, ssa_op, left->dest, right->dest);
  return inst;
}

static void dump(SsaVisitor *v, FILE *out) {
  for (int i = 0; i < v->instructions_size; i++) {
    fprintf(out, "%s\n", v->instructions[i].text);
  }
}

static void dump_result(FILE *out, SsaInstruction *expr) {
  fprintf(out, "t%d", expr->dest);
}

static void *visit_declarator(SsaVisitor *v, int index, int offset) {
  // make a new temp
  SsaInstruction *inst = new_instruction(v);
  checked_asprintf(&inst->text, "allocate local #%d  # top of stack = stack-%d", index, offset);
  return inst;
}

static void *visit_function_definition(SsaVisitor *v, int function_name, const Type *function_type) {
  SsaInstruction *inst = new_instruction(v);
  checked_asprintf(&inst->text, ".Lfunction_%d  # return type %d, %d parameters", function_name, function_type->return_type->kind, function_type->n_params);
  return inst;
}

VisitorHeader *new_ssa_visitor() {
  SsaVisitor *v = checked_calloc(1, sizeof(SsaVisitor));
  NEW_VECTOR(v->instructions, sizeof(SsaInstruction));
  INSTALL_VISITOR_METHODS(v)
  return (VisitorHeader *) v;
}
