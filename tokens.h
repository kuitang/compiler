#pragma once
#include "vendor/map.h"

// These macros are only local to this file, so we use lowecase and underscored names and undef them when done.
#define keywords_ \
alignas,\
alignof,\
auto,\
bool,\
break,\
case,\
char,\
const,\
constexpr,\
continue,\
default,\
do,\
double,\
else,\
enum,\
extern,\
false,\
float,\
for,\
goto,\
if,\
inline,\
int,\
long,\
nullptr,\
register,\
restrict,\
return,\
short,\
signed,\
sizeof,\
static,\
static_assert,\
struct,\
switch,\
thread_local,\
true,\
typedef,\
typeof,\
typeof_unqual,\
union,\
unsigned,\
void,\
volatile,\
while,\
_Alignas,\
_Alignof,\
_Atomic,\
_BitInt,\
_Bool,\
_Complex,\
_Decimal128,\
_Decimal32,\
_Decimal64,\
_Generic,\
_Imaginary,\
_Noreturn,\
_Static_assert,\
_Thread_local

#define punct_literals_ \
...,\
>>=,\
<<=,\
+=,\
-=,\
*=,\
/=,\
%=,\
&=,\
^=,\
|=,\
>>,\
<<,\
++,\
--,\
->,\
&&,\
||,\
<=,\
>=,\
==,\
!=,\
;,\
{,\
},\
,,\
:,\
=,\
(,\
),\
[,\
],\
.,\
&,\
!,\
~,\
-,\
+,\
*,\
/,\
%,\
<,\
>,\
^,\
|,\
?

#define punct_names_ \
ELLIPSIS,\
RIGHT_ASSIGN,\
LEFT_ASSIGN,\
ADD_ASSIGN,\
SUB_ASSIGN,\
MUL_ASSIGN,\
DIV_ASSIGN,\
MOD_ASSIGN,\
AND_ASSIGN,\
XOR_ASSIGN,\
OR_ASSIGN,\
RIGHT_OP,\
LEFT_OP,\
INC_OP,\
DEC_OP,\
PTR_OP,\
AND_OP,\
OR_OP,\
LE_OP,\
GE_OP,\
EQ_OP,\
NE_OP,\
SEMI,\
LEFT_BRACE,\
RIGHT_BRACE,\
COMMA,\
COLON_OP,\
ASSIGN_OP,\
LEFT_PAREN,\
RIGHT_PAREN,\
LEFT_BRACKET,\
RIGHT_BRACKET,\
DOT_OP,\
AMPERSAND_OP,\
NOT_OP,\
COMPL_OP,\
SUB_OP,\
ADD_OP,\
STAR_OP,\
DIV_OP,\
MOD_OP,\
LT_OP,\
RT_OP,\
XOR_OP,\
BIT_OR_OP,\
QUESTION_OP

#define other_tokens_ \
ERROR,\
END_OF_FILE,\
IDENT,\
STRING_LITERAL,\
INTEGER_LITERAL,\
FLOAT_LITERAL

#define enum_line_(name) TOK_##name,
#define string_line_(name) "TOK_" #name,

typedef enum {
  MAP(enum_line_, other_tokens_)
  TOK_SEPARATOR_KEYWORDS,
  MAP(enum_line_, keywords_)
  TOK_SEPARATOR_PUNCT,
  MAP(enum_line_, punct_names_)
} TokenKind;

const char *TOKEN_NAMES[] = {
  MAP(string_line_, other_tokens_)
  "TOK_SEPARATOR_KEYWORDS",
  MAP(string_line_, keywords_)
  "TOK_SEPARATOR_PUNCT",
  MAP(string_line_, punct_names_)
};

#define set_size_(size, storage) const int size = sizeof(storage) / sizeof(char *)
set_size_(N_TOKENS, TOKEN_NAMES);

const char *KEYWORD_NAMES[] = {
  MAP(string_line_, keywords_)
};
set_size_(N_KEYWORDS, KEYWORD_NAMES);

const char *PUNCT_NAMES[] = {
  MAP(string_line_, punct_names_)
};
set_size_(N_PUNCTS, PUNCT_NAMES);

// helper macros
#define KEYWORD_IX_TO_TOKEN_KIND(ix) ((ix) + TOK_SEPARATOR_KEYWORDS + 1)
#define PUNCT_IX_TO_TOKEN_KIND(ix) ((ix) + TOK_SEPARATOR_PUNCT + 1)

#undef keywords_
#undef punct_literals_
#undef punct_names_
#undef other_tokens_
#undef enum_line_
#undef string_line_
