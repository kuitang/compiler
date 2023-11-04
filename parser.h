#pragma once
#include "lexer.h"
#include <stdio.h>

typedef struct Visitor Visitor;
typedef struct ParserCont ParserCont;

ParserCont *new_parser_cont(FILE *in, const char *filename, Visitor *visitor);
// TODO: Expose methods to parse strings for testing
void parse_translation_unit(ParserCont *cont);
void init_parser_module();
Token peek(ParserCont *cont);
const char *peek_str(ParserCont *cont);
