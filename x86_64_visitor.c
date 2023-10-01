#include <assert.h>
#include "visitor.h"
#include "types.h"
#include "stdio.h"
#include "common.h"

// every value has a PERMANENT location that is not in a register.
typedef enum {
  LOC_NONE = 0,
  LOC_IMMEDIATE,
  LOC_STACK,
  LOC_GLOBAL,
} LocationKind;

typedef struct {
  const Type *type;
  LocationKind location_kind;
  union {
    int rbp_offset;
    const char *global_name;
    int64_t integer_immediate;
    double float_immediate;
  };
  int in_accum;  // if it is TEMPORARILY in the accumulator
  int in_flags;  // ... or flags register
  const char *debug_name;
} x86_64_Value;

typedef struct {
  Visitor _visitor;
  // current contents
  x86_64_Value *flags_contents;
  x86_64_Value *accum_contents;  // are they the same now?
  int curr_rbp_offset;
  int curr_temp_id;
  int curr_func_param;
  FILE *out;  // should be a memstream
  const Type *curr_func_return_type;
} x86_64_Visitor;

int next_aligned_offset(int curr_offset, int size, int alignment) {
  int new_offset = curr_offset + size;
  new_offset += new_offset % alignment;
  return new_offset;
}

// generalize to more registers
static char *accum_register(int size) {
  switch (size) {
    case 1:
      return "%al";
    case 2:
      return "%ax";
    case 4:
      return "%eax";
    case 8:
      return "%rax";
    default:
      THROWF(EXC_INTERNAL, "Unsupported value size %d", size);
  }
}

/*
static char *other_register(int size) {
  switch (size) {
    case 1:
      return "%dl";
    case 2:
      return "%dx";
    case 4:
      return "%edx";
    case 8:
      return "%rdx";
    default:
      THROWF(EXC_INTERNAL, "Unsupported value size %d", size);
  }
}
*/

static char suffixes[] = {
  [1] = 'b',
  [2] = 's',
  [4] = 'l',
  [8] = 'q',
};

static const char param_registers[][6][5] = {
  [1] = {"%dil", "%sil", "%dl", "%cl", "%r8b", "%r9b"},
  [2] = {"%di", "%si", "%dx", "%cx", "%r8w", "%r9w"},
  [4] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"},
  [8] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"},
};

static const char *operator(char *op, int size) {
  return fmtstr("%s%c", op, suffixes[size]);
}

static const char *addr(x86_64_Value *val) {
  switch (val->location_kind) {
    case LOC_IMMEDIATE:
      return fmtstr("$%lld", val->integer_immediate);
      break;
    case LOC_STACK:
      return fmtstr("%d(%%rbp)", val->rbp_offset);
    case LOC_GLOBAL:
      return val->global_name;
    default:
      THROWF(EXC_INTERNAL, "Unsupported location %d", val->location_kind);
  }
}

static char *binary_template = "\t%s\t%s, %s\t\t# %s = %s\n";

/**
 * Allocate a temporary variable holding a value of type on the stack. The contents are still UNDEFINED and must be
 * filled in subsequent instructions.
 *
 * This function does NOT modify rsp; that will be done right before the subroutine call (if any). Further, the stack
 * is not required to be aligned.
 */

static x86_64_Value *new_variable(x86_64_Visitor *v, const Type *type, const char *debug_name) {
  v->curr_rbp_offset -= type->size;
  v->curr_temp_id++;

  x86_64_Value *ret = checked_calloc(1, sizeof(x86_64_Value));
  ret->location_kind = LOC_STACK;
  ret->rbp_offset = v->curr_rbp_offset;
  ret->type = type;
  ret->debug_name = debug_name ? debug_name : fmtstr("t%d", v->curr_temp_id);

  fprintf(v->out, "\tsubq\t$%d, %%rsp  # %s at %d(%%rbp) \n", ret->type->size, ret->debug_name, ret->rbp_offset);
  return ret;
}

static void copy_from_accum(x86_64_Visitor *v, x86_64_Value *val) {
  v->accum_contents = val;
  val->in_accum = 1;
  int size = val->type->size;
  fprintf(
    v->out,
    binary_template,
    operator("mov", size), accum_register(size), addr(val),
    fmtstr("%s = %s", val->debug_name, addr(val)), accum_register(size)
  );
}

static void copy_to_accum(x86_64_Visitor *v, x86_64_Value *val) {
  if (val->in_accum) {
    assert(v->accum_contents == val);
    return;  // do nothing
  }
  int size = val->type->size;
  v->accum_contents = val;
  fprintf(
    v->out,
    binary_template,
    operator("mov", size), addr(val), accum_register(size),
    accum_register(size), val->debug_name
  );
}

static Type INTEGER_LITERAL_TYPE = {
  .kind = TY_INTEGER,
  .size = 4,
  .alignment = 4,
};

static x86_64_Value *visit_integer_literal(x86_64_Visitor *v, int64_t int64_val) {
  x86_64_Value *ret = checked_calloc(1, sizeof(x86_64_Value));
  ret->location_kind = LOC_IMMEDIATE;
  ret->integer_immediate = int64_val;
  ret->debug_name = addr(ret);
  ret->type = &INTEGER_LITERAL_TYPE;
  return ret;
}

static x86_64_Value *visit_float_literal(x86_64_Visitor *v, int64_t int64_val) {
  assert(0 && "Unimplemented!");
}

static x86_64_Value *convert_type(x86_64_Value *value, const Type *new_type) {
  // assert((compare_type(value->type, new_type) != 0) && "Unnecessary convert_type call");
  // handle all the cases later
  x86_64_Value *ret = checked_calloc(1, sizeof(x86_64_Value));

  if (value->location_kind == LOC_IMMEDIATE) {
    // Copy everything
    // TODO: Handle other cases later...
    *ret = *value;
    if (value->type->kind == TY_INTEGER && new_type->kind == TY_FLOAT) {
      value->float_immediate = (double) value->integer_immediate;
    }
  }
  return ret;
}

static void *visit_assign(x86_64_Visitor *v, TokenKind op, x86_64_Value *left, x86_64_Value *right) {
  // stupid version for now
  fprintf(v->out, "\t# DEBUG: visit_assign %s = %s\n", left->debug_name, right->debug_name);
  copy_to_accum(v, right);
  copy_from_accum(v, left);
  return left;
}

static void *visit_binop(x86_64_Visitor *v, TokenKind op, x86_64_Value *left, x86_64_Value *right) {
  const Type *type = left->type;
  int size = type->size;
  const char *accum_reg = accum_register(size);
  const char *left_operand = addr(left);
  x86_64_Value *result = new_variable(v, type, 0);
  switch (op) {
    case TOK_ADD_OP:
      copy_to_accum(v, right);
      fprintf(
        v->out,
        binary_template,
        operator("add", size), left_operand, accum_reg,
        accum_reg, fmtstr("%s + %s", left->debug_name, right->debug_name)
      );
      copy_from_accum(v, result);
      break;
    case TOK_SUB_OP:
      copy_to_accum(v, right);
      fprintf(
        v->out,
        binary_template,
        operator("sub", size), left_operand, accum_reg,
        accum_reg, fmtstr("%s - %s", left->debug_name, right->debug_name)
      );
      copy_from_accum(v, result);
      break;
    case TOK_STAR_OP:
      copy_to_accum(v, right);
      fprintf(
        v->out,
        binary_template,
        operator("imul", size), left_operand, accum_reg,
        accum_reg, fmtstr("%s * %s", left->debug_name, right->debug_name)
      );
      copy_from_accum(v, result);
      break;
    case TOK_DIV_OP:
      copy_to_accum(v, left);
      char *ct;
      switch (size) {
        case 2: ct = "cwtd"; break;
        case 4: ct = "cltd"; break;
        case 8: ct = "cqto"; break;
        default: THROWF(EXC_INTERNAL, "wrong size for division: %d", size);
      }
      fprintf(v->out, "\t%s\n", ct);
      fprintf(
        v->out,
        "\t%s\t%s\t\t# %s = %s\n",
        operator("idiv", size), addr(right),
        accum_reg,
        fmtstr("%s / %s", left->debug_name, right->debug_name)
      );
      copy_from_accum(v, result);
      break;
    case TOK_COMMA:
      // special case; just return right
      return right;
    default:
      THROWF(EXC_INTERNAL, "Binop %s not supported", TOKEN_NAMES[op]);
  }
  return result;
}

x86_64_Value *visit_conditional(x86_64_Visitor *v, TokenKind op, int jump, void *left, void *right) {
  assert(0 && "Unimplemented!");
}

// http://6.s081.scripts.mit.edu/sp18/x86-64-architecture-guide.html
static char *prologue = "\t.globl	_%s\n_%s:\n\tpushq\t%%rbp\n\tmovq\t%%rsp, %%rbp\n";
static void visit_function_definition_start(
  x86_64_Visitor *v,
  DeclarationSpecifiers decl_specs,
  Declarator *declarator
) {
  v->curr_func_param = 0;
  fprintf(v->out, prologue, declarator->ident_string, declarator->ident_string);
}

static void *visit_declaration(
  x86_64_Visitor *v,
  DeclarationSpecifiers decl_specs,
  Declarator *declarator
) {
  x86_64_Value *ret = new_variable(v, decl_specs.type, declarator->ident_string);
  return ret;
}

static void *visit_function_definition_param(
  x86_64_Visitor *v,
  DeclarationSpecifiers decl_specs,
  Declarator *declarator
) {
  x86_64_Value *ret = visit_declaration(v, decl_specs, declarator);
  int size = ret->type->size;
  fprintf(v->out, "\t%s\t%s, %s\n", operator("mov", size), param_registers[size][v->curr_func_param], addr(ret));
  v->curr_func_param++;
  return ret;
}

static void visit_return(x86_64_Visitor *v, void *retval) {
  copy_to_accum(v, retval);
  fputs("\tleave\n\tretq\n", v->out);
}

static void visit_function_end(x86_64_Visitor *v) {
  fputs("\tleave\n\tretq\n", v->out);
}


static void finalize(x86_64_Visitor *v) {
  checked_fclose(v->out);
}

Visitor *new_x86_64_visitor(FILE *out) {
  x86_64_Visitor *v = checked_calloc(1, sizeof(x86_64_Visitor));
  v->curr_rbp_offset = 0;
  v->curr_temp_id = 0;
  v->out = out;
  INSTALL_VISITOR_METHODS(v)
  fputs("# KUI'S COMPILER\n", v->out);
  return (Visitor *) v;
}
