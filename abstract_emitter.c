#include "emitter.h"
#include "tokens.h"
#include "common.h"
#include <assert.h>

static char *MachineTypeNames[] = {
  "i64",
  "double"
};

MachineType emit_immediate_int64(EmitterCont *cont, int64_t val) {
  fprintf(cont->out, "load %%i64[0], %lld\n", val);
  fprintf(cont->out, "push[i64] %%i64[0]\n");
  return MT_INT64;
}

MachineType emit_immediate_double(EmitterCont *cont, double val) {
  fprintf(cont->out, "load[double] %%double[0], %g\n", val);
  fprintf(cont->out, "push[double] %%double[0]\n");
  return MT_DOUBLE;
}

MachineType emit_binop_i64_i64(EmitterCont *cont, TokenKind op, int reversed) {
  fprintf(cont->out, "# emit_binop_i64_i64 op=%s, reversed=%d\n", TOKEN_NAMES[op], reversed);
  if (reversed) {
    // TOS = lhs; TOS - 1 = rhs
    fprintf(cont->out, "pop[i64] %%i64[0]\n"); // rhs
    fprintf(cont->out, "pop[i64] %%i64[1]\n"); // rhs
  } else {
    // TOS = rhs; TOS - 1 = lhs
    fprintf(cont->out, "pop[i64] %%i64[1]\n"); // rhs
    fprintf(cont->out, "pop[i64] %%i64[0]\n"); // rhs
  }
  switch (op) {
    case TOK_ADD_OP:
      fprintf(cont->out, "add[i64] %%i64[0] %%i64[1]\n");
      break;
    case TOK_SUB_OP:
      fprintf(cont->out, "sub[i64] %%i64[0] %%i64[1]\n");
      break;
    case TOK_STAR_OP:
      // NOTE: This is not straightforward for AMD64
      fprintf(cont->out, "mult[i64] %%i64[0] %%i64[1]\n");
      break;
    case TOK_DIV_OP:
      // NOTE: This is not straightforward for AMD64
      fprintf(cont->out, "div[i64] %%i64[0] %%i64[1]\n");
      break;
    default:
      THROW(EXC_EMITTER, "Unsupported operator");
  }
  return MT_INT64;
}

// this looks like a copy paste, but for AMD64 will be different
MachineType emit_binop_double_double(EmitterCont *cont, TokenKind op, int reversed) {
  fprintf(cont->out, "# emit_binop_double_double op=%s, reversed=%d\n", TOKEN_NAMES[op], reversed);
  if (reversed) {
    // TOS = lhs; TOS - 1 = rhs
    fprintf(cont->out, "pop[double] %%double[0]\n"); // rhs
    fprintf(cont->out, "pop[double] %%double[1]\n"); // rhs
  } else {
    // TOS = rhs; TOS - 1 = lhs
    fprintf(cont->out, "pop[double] %%double[1]\n"); // rhs
    fprintf(cont->out, "pop[double] %%double[0]\n"); // rhs
  }
  switch (op) {
    case TOK_ADD_OP:
      fprintf(cont->out, "add[double] %%double[0] %%double[1]\n");
      break;
    case TOK_SUB_OP:
      fprintf(cont->out, "sub[double] %%double[0] %%double[1]\n");
      break;
    case TOK_STAR_OP:
      // NOTE: This is not straightforward for AMD64
      fprintf(cont->out, "mult[double] %%double[0] %%double[1]\n");
      break;
    case TOK_DIV_OP:
      // NOTE: This is not straightforward for AMD64
      fprintf(cont->out, "div[double] %%double[0] %%double[1]\n");
      break;
    default:
      THROW(EXC_EMITTER, "Unsupported operator");
  }
  return MT_DOUBLE;
}

// The amd64 emitter can skip the stack ceremony and directly use register, I think.
// But the stack ceremony is easier to reason about
static void promote_i64_to_double(EmitterCont *cont) {
  fprintf(cont->out, "# promote_i64_to_double\n");
  fprintf(cont->out, "# promote_i64_to_double\n");
  fprintf(cont->out, "pop[i64], %%i64[0]\n");
  fprintf(cont->out, "promote_i64_double %%double[0], %%i64[0]\n");
  fprintf(cont->out, "push[double] %%double[0]\n");
}

static void swap_tos(EmitterCont *cont, MachineType tos_ty, MachineType next_ty) {
  assert(tos_ty != next_ty);
  char *s_tos_ty = MachineTypeNames[tos_ty];
  char *s_next_ty = MachineTypeNames[next_ty];
  fprintf(cont->out, "# swap_stack tos_ty = %s, next_ty = %s\n", s_tos_ty, s_next_ty);
  fprintf(cont->out, "pop[%s] %%%s[0]\n", s_tos_ty, s_tos_ty);
  fprintf(cont->out, "pop[%s] %%%s[0]\n", s_next_ty, s_next_ty);
  fprintf(cont->out, "push[%s] %%%s[0]\n", s_next_ty, s_next_ty);
  fprintf(cont->out, "push[%s] %%%s[0]\n", s_tos_ty, s_tos_ty);
}


MachineType emit_binop(EmitterCont *cont, TokenKind op, MachineType lhs_ty, MachineType rhs_ty) {
  if (lhs_ty == MT_INT64 && rhs_ty == MT_INT64) {
    return emit_binop_i64_i64(cont, op, 0);
  } else if (lhs_ty == MT_DOUBLE && rhs_type == MT_DOUBLE) {
    return emit_binop_double_double(cont, op, 0);
  } else if (lhs_ty == MT_DOUBLE && rhs_ty == MT_INT64) {
    // TOS     = int64 rhs;
    // TOS - 1 = double lhs;
    promote_i64_to_double(cont);
    // TOS     = double rhs;
    // TOS - 1 = double lhs;
    emit_binop_(cont, op, MT_DOUBLE, MT_DOUBLE, 0);
    return MT_DOUBLE;
  } else if (lhs_ty == MT_INT64 && rhs_ty == MT_DOUBLE) {
    // Current state:
    // TOS     = double rhs;
    // TOS - 1 = int64 lhs;
    swap_tos(MT_DOUBLE, MT_INT64);
    // TOS     = int64 lhs;
    // TOS -1  = double rhs;
    promote_i64_to_double(cont);
    // TOS     = double lhs;
    // TOS -1  = double rhs;
    emit_binop(cont, MT_DOUBLE, MT_DOUBLE, 1);
    return MT_DOUBLE;
  }
  
}
