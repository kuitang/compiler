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
f("...",ELLIPSIS)\
f(">>=",RIGHT_ASSIGN)\
f("<<=",LEFT_ASSIGN)\
f("+=",ADD_ASSIGN)\
f("-=",SUB_ASSIGN)\
f("*=",MUL_ASSIGN)\
f("/=",DIV_ASSIGN)\
f("%=",MOD_ASSIGN)\
f("&=",AND_ASSIGN)\
f("^=",XOR_ASSIGN)\
f("|=",OR_ASSIGN)\
f(">>",RIGHT_OP)\
f("<<",LEFT_OP)\
f("++",INC_OP)\
f("--",DEC_OP)\
f("->",PTR_OP)\
f("&&",AND_OP)\
f("||",OR_OP)\
f("<=",LE_OP)\
f(">=",GE_OP)\
f("==",EQ_OP)\
f("!=",NE_OP)\
f(";",SEMI)\
f("{",LEFT_BRACE)\
f("}",RIGHT_BRACE)\
f(",",COMMA)\
f(":",COLON_OP)\
f("=",ASSIGN_OP)\
f("(",LEFT_PAREN)\
f(")",RIGHT_PAREN)\
f("[",LEFT_BRACKET)\
f("]",RIGHT_BRACKET)\
f(".",DOT_OP)\
f("&",AMPERSAND_OP)\
f("!",NOT_OP)\
f("~",COMPL_OP)\
f("-",SUB_OP)\
f("+",ADD_OP)\
f("*",STAR_OP)\
f("/",DIV_OP)\
f("%",MOD_OP)\
f("<",LT_OP)\
f(">",RT_OP)\
f("^",XOR_OP)\
f("|",BIT_OR_OP)\
f("?",QUESTION_OP)

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

const char *TOKEN_NAMES[] = {
  other_tokens_(tok_string_line_)
  "TOK_SEPARATOR_KEYWORDS",
  keywords_(tok_string_line_)
  "TOK_SEPARATOR_PUNCT",
  puncts_(tok_string_line1_)
};

#define set_size_(size, storage) const int size = sizeof(storage) / sizeof(char *)
set_size_(N_TOKENS, TOKEN_NAMES);

const char *KEYWORD_VALUES[] = {
  keywords_(raw_string_line_)
};
set_size_(N_KEYWORDS, KEYWORD_VALUES);

const char *PUNCT_VALUES[] = {
  puncts_(id_string_line_0_)
};
set_size_(N_PUNCTS, PUNCT_VALUES);

// helper macros
// WARNING: Who sorts? If sorts change the order of any of the arrays above, we fail.
#define KEYWORD_KIND(ix) ((ix) + TOK_SEPARATOR_KEYWORDS + 1)
#define PUNCT_KIND(ix) ((ix) + TOK_SEPARATOR_PUNCT + 1)
#define KEYWORD_NAME(ix) TOKEN_NAMES[KEYWORD_KIND(ix)]
#define PUNCT_NAME(ix) TOKEN_NAMES[PUNCT_NAME(ix)]

#undef keywords_
#undef puncts_
#undef other_tokens_
#undef tok_enum_line_
#undef raw_string_line_
#undef tok_string_line_
#undef tok_enum_line_1_
#undef tok_string_line_1_
#undef id_string_line_0_
#undef set_size_
