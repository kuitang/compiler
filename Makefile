# all: kuicc_program.s clan_program.s clang_opt_program.s
CC = clang
CFLAGS = -O0 -g3 -std=c11 -Wall -Wextra -Werror -Wpedantic -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer
# all: clang_program.s clang_opt_program.s

all: run golden/prog1_trace.txt golden/prog2_parse.txt golden/one_plus_two_parse.txt golden/one_plus_two_ast.txt golden/prog2_ast.txt golden/floating_expr_ast.txt golden/floating_expr_parse.txt

golden/one_plus_two_parse.txt: parser_driver golden/one_plus_two.c
	rm -f $@
	./parser_driver -v ssa golden/one_plus_two.c 2>/dev/null > $@
	git --no-pager diff --color-words $@

golden/prog2_parse.txt: parser_driver golden/prog2.c
	rm -f $@
	./parser_driver -v ssa golden/prog2.c  2>/dev/null > $@
	git --no-pager diff --color-words $@

golden/one_plus_two_ast.txt: parser_driver golden/one_plus_two.c
	rm -f $@
	./parser_driver -v ast golden/one_plus_two.c 2>/dev/null > $@
	git --no-pager diff --color-words $@

golden/floating_expr_ast.txt: parser_driver golden/floating_expr.c
	rm -f $@
	./parser_driver -v ast golden/floating_expr.c 2>/dev/null > $@
	git --no-pager diff --color-words $@

golden/floating_expr_parse.txt: parser_driver golden/floating_expr.c
	rm -f $@
	./parser_driver -v ssa golden/floating_expr.c 2>/dev/null > $@
	git --no-pager diff --color-words $@

golden/prog2_ast.txt: parser_driver golden/prog2.c
	rm -f $@
	./parser_driver -v ast golden/prog2.c  2>/dev/null > $@
	git --no-pager diff --color-words $@

golden/prog1_trace.txt: lexer_driver golden/prog1.c
	rm -f $@
	./lexer_driver golden/prog1.c 2>/dev/null > $@
	git --no-pager diff --color-words $@

golden/one_plus_two.s: parser_driver golden/one_plus_two.c
	rm -f $@
	./parser_driver -v x86_64 -o golden/one_plus_two.s golden/one_plus_two.c
	git --no-pager diff --color-words $@

golden/declarations.s: parser_driver golden/declarations.c
	rm -f $@
	./parser_driver -v ssa -o golden/declarations.s golden/declarations.c
	git --no-pager diff --color-words $@

driver: driver.c golden/one_plus_two.s
	clang -o $@ driver.c golden/one_plus_two.s

driver_clang: driver.c golden/one_plus_two_clang.s
	clang -o $@ driver.c golden/one_plus_two_clang.s

run: driver driver_clang
	./driver
	./driver_clang

# TODO: When we can compile functions, remove the header
golden/one_plus_two_clang.s: golden/one_plus_two.c
	echo "long long f() { return" > tmp/t1.c
	cat golden/one_plus_two.c >> tmp/t1.c
	echo "; }" >> tmp/t1.c
	clang -O0 -S tmp/t1.c -o $@

parser_driver: parser_driver.c x86_64_visitor.o lexer.o common.o

lexer_driver: lexer_driver.c lexer.o common.o

x86_64_visitor.o: x86_64_visitor.c common.h

lexer.o: lexer.c common.h

common.o: common.c common.h

# gen_tokens: gen_tokens.c common.h
# 	$(CC) $(CFLAGS) $< -o $@

# kuicc: kuicc.c
# 	$(CC) $(CFLAGS) $< -o $@

# driver: kuicc_program.s driver.c
# 	clang kuicc_program.s driver.c

# driver: clang_program.s driver.c
# 	clang clang_program.s driver.c -o driver

# program.s: kuicc program.c
# 	./kuicc < program.c > program.s

# clang_program.s: program.c
# 	clang -S program.c -o clang_program.s

# clang_opt_program.s: program.c
# 	clang -O3 -S program.c -o clang_opt_program.s


.PHONY: clean run
clean:
	rm -rf *.i *.s *.o *.gch *.dSYM *driver driver_clang kuicc a.out golden/*.s

