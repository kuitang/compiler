CC = clang
CFLAGS = -O0 -g3 -std=c11 -Wall -Wextra -Werror -Wpedantic -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer
# all: clang_program.s clang_opt_program.s

# all: run golden/prog1_trace.txt golden/prog2_parse.txt golden/one_plus_two_parse.txt golden/one_plus_two_ast.txt golden/prog2_ast.txt golden/floating_expr_ast.txt golden/floating_expr_parse.txt
all: \
	main \
	golden/prog1_trace.txt \
	run_arrays \
	run_int_func \
	run_one_plus_two \
	run_structs	

golden/prog1_trace.txt: lexer_main golden/prog1.c
	rm -f $@
	./lexer_main golden/prog1.c 2>/dev/null > $@
	git --no-pager diff --color-words $@

golden/%.s: golden/%.c main
	./main -o $@ $< 2>/dev/null
	git --no-pager diff --color-words $@

%_driver: golden/%_driver.c golden/%.s
	$(CC) -o $@ $^

%_driver_clang: golden/%_driver.c golden/%.c
	$(CC) -o $@ $^

run_%: %_driver %_driver_clang
	echo "KUI'S RESULT"
	./$<
	echo "CLANG'S RESULT"
	./$(word 2,$^)

main: main.c x86_64_visitor.o common.o parser.o lexer.o types_impl.o

lexer_main: lexer_main.c lexer.o common.o

types_impl.o: types_impl.c common.h

parser.o: parser.c common.h

x86_64_visitor.o: x86_64_visitor.c common.h

lexer.o: lexer.c common.h

common.o: common.c common.h

# golden/one_plus_two_parse.txt: main golden/one_plus_two.c
# 	rm -f $@
# 	./main -v ssa golden/one_plus_two.c 2>/dev/null > $@
# 	git --no-pager diff --color-words $@

# golden/prog2_parse.txt: main golden/prog2.c
# 	rm -f $@
# 	./main -v ssa golden/prog2.c  2>/dev/null > $@
# 	git --no-pager diff --color-words $@

# golden/one_plus_two_ast.txt: main golden/one_plus_two.c
# 	rm -f $@
# 	./main -v ast golden/one_plus_two.c 2>/dev/null > $@
# 	git --no-pager diff --color-words $@

# golden/floating_expr_ast.txt: main golden/floating_expr.c
# 	rm -f $@
# 	./main -v ast golden/floating_expr.c 2>/dev/null > $@
# 	git --no-pager diff --color-words $@

# golden/floating_expr_parse.txt: main golden/floating_expr.c
# 	rm -f $@
# 	./main -v ssa golden/floating_expr.c 2>/dev/null > $@
# 	git --no-pager diff --color-words $@

# golden/prog2_ast.txt: main golden/prog2.c
# 	rm -f $@
# 	./main -v ast golden/prog2.c  2>/dev/null > $@
# 	git --no-pager diff --color-words $@

.PHONY: clean run
clean:
	rm -rf *.i *.s *.o *.gch *.dSYM *driver* a.out golden/*.s

