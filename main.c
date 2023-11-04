#include "common.h"
#include "parser.h"
#include "visitor.h"
#include <unistd.h>
#include <string.h>

extern char *optarg;
extern int optind;
extern int optopt;
extern int opterr;
extern int optreset;

static void parse_start(FILE *in, const char *filename, Visitor *visitor) {
  ParserCont *cont = new_parser_cont(in, filename, visitor);
  if (setjmp(global_exception_handler) == 0) {
    parse_translation_unit(cont);
    visitor->finalize(visitor);

    if (peek(cont).kind != TOK_END_OF_FILE) {
      fprintf(stderr, "ERROR: Parser did not reach EOF; next token is %s\n", peek_str(cont));
    } else {
      fprintf(stderr, "Translation unit parsed completely and successfully!\n");
    }
  } else {
    PRINT_EXCEPTION();
    abort();
  }
}

void usage() {
  fprintf(stderr, "usage: parser_driver [options] file\n\n");
  fprintf(stderr, "options:\n");
  fprintf(stderr, "  -v <visitor> which visitor to use (choices: ssa, ast, x86_64)\n");
  fprintf(stderr, "  -o <file>    save output to this file\n");
  exit(1);
}

extern Visitor *new_x86_64_visitor(FILE *out);

int main(int argc, char *argv[]) {
  FILE *out = stdout;
  VisitorConstructor visitor_ctor = 0;
  int ch;
  while ((ch = getopt(argc, argv, "v:o:")) != -1) {
    switch (ch) {
      case 'v':
        if (strcmp(optarg, "ssa") == 0) {
          // visitor_ctor = new_ssa_visitor;
          usage();
          // break;
        } else if (strcmp(optarg, "ast") == 0) {
          // visitor_ctor = new_ast_visitor;
          usage();
          // break;
        } else if (strcmp(optarg, "x86_64") == 0) {
          visitor_ctor = new_x86_64_visitor;
          break;
        }
      case 'o':
        out = checked_fopen(optarg, "w");
        break;
      case '?':
      default:
        usage();
    }
  }
  argc -= optind;
  argv += optind;
  if (argc < 1) {
    usage();
  }

  visitor_ctor = visitor_ctor ? visitor_ctor : new_x86_64_visitor;
  FILE *in = checked_fopen(argv[0], "r");
  init_parser_module();

  parse_start(in, argv[0], visitor_ctor(out));
  return 0;
}
