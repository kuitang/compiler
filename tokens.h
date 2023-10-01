#pragma once

// These macros are only local to this file, so we use lowecase and underscored names and undef them when done.
#define keywords_(f) \
f(alignas)\
f(alignof)\
f(auto)\
f(bool)\
f(break)\
f(case)\
f(char)\
f(const)\
f(constexpr)\
f(continue)\
f(default)\
f(do)\
f(double)\
f(else)\
f(enum)\
f(extern)\
f(false)\
f(float)\
f(for)\
f(goto)\
f(if)\
f(inline)\
f(int)\
f(long)\
f(nullptr)\
f(register)\
f(restrict)\
f(return)\
f(short)\
f(signed)\
f(sizeof)\
f(static)\
f(static_assert)\
f(struct)\
f(switch)\
f(thread_local)\
f(true)\
f(typedef)\
f(typeof)\
f(typeof_unqual)\
f(union)\
f(unsigned)\
f(void)\
f(volatile)\
f(while)\
f(_Alignas)\
f(_Alignof)\
f(_Atomic)\
f(_BitInt)\
f(_Bool)\
f(_Complex)\
f(_Decimal128)\
f(_Decimal32)\
f(_Decimal64)\
f(_Generic)\
f(_Imaginary)\
f(_Noreturn)\
f(_Static_assert)\
f(_Thread_local)

#define puncts_(f) \
f("!",NOT_OP)\
f("!=",NE_OP)\
f("%",MOD_OP)\
f("%=",MOD_ASSIGN)\
f("&",AMPERSAND_OP)\
f("&&",AND_OP)\
f("&=",AND_ASSIGN)\
f("(",LEFT_PAREN)\
f(")",RIGHT_PAREN)\
f("*",STAR_OP)\
f("*=",MUL_ASSIGN)\
f("+",ADD_OP)\
f("++",INC_OP)\
f("+=",ADD_ASSIGN)\
f(",",COMMA)\
f("-",SUB_OP)\
f("--",DEC_OP)\
f("-=",SUB_ASSIGN)\
f("->",PTR_OP)\
f(".",DOT_OP)\
f("...",ELLIPSIS)\
f("/",DIV_OP)\
f("/=",DIV_ASSIGN)\
f(":",COLON_OP)\
f(";",SEMI)\
f("<",LT_OP)\
f("<<",LEFT_OP)\
f("<<=",LEFT_ASSIGN)\
f("<=",LE_OP)\
f("=",ASSIGN_OP)\
f("==",EQ_OP)\
f(">",RT_OP)\
f(">=",GE_OP)\
f(">>",RIGHT_OP)\
f(">>=",RIGHT_ASSIGN)\
f("?",QUESTION_OP)\
f("[",LEFT_BRACKET)\
f("]",RIGHT_BRACKET)\
f("^",XOR_OP)\
f("^=",XOR_ASSIGN)\
f("{",LEFT_BRACE)\
f("|",BIT_OR_OP)\
f("|=",OR_ASSIGN)\
f("||",OR_OP)\
f("}",RIGHT_BRACE)\
f("~",COMPL_OP)

#define other_tokens_(f) \
f(ERROR)\
f(END_OF_FILE)\
f(IDENT)\
f(STRING_LITERAL)\
f(INTEGER_LITERAL)\
f(FLOAT_LITERAL)

#define tok_enum_line_(name) TOK_##name,
#define raw_string_line_(name) #name,
#define tok_string_line_(name) "TOK_" #name,

#define tok_enum_line1_(_, name) tok_enum_line_(name)
#define tok_string_line1_(_, name) tok_string_line_(name)
#define id_string_line_0_(lit, _) lit,

typedef enum {
  other_tokens_(tok_enum_line_)
  TOK_SEPARATOR_KEYWORDS,
  keywords_(tok_enum_line_)
  TOK_SEPARATOR_PUNCT,
  puncts_(tok_enum_line1_)
} TokenKind;

#undef tok_enum_line_
#undef tok_enum_line1_

extern const char *TOKEN_NAMES[];
