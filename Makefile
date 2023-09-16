# all: kuicc_program.s clan_program.s clang_opt_program.s
CC = clang
CFLAGS = -O0 -g -std=c11 -Wall -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer
# all: clang_program.s clang_opt_program.s

all: golden/prog1_trace.txt golden/prog2_abstract_code.txt

golden/prog2_abstract_code.txt: parser_driver golden/prog2.c
	./parser_driver golden/prog2.c > $@
	git --no-pager diff --color-words $@

golden/prog1_trace.txt: lexer_driver golden/prog1.c
	./lexer_driver golden/prog1.c > $@
	git --no-pager diff --color-words $@

parser_driver: parser_driver.c lexer.o common.o

lexer_driver: lexer_driver.c lexer.o common.o

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
	rm -f *.s *.o *driver kuicc a.out

