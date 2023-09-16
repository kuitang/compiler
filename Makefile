# all: kuicc_program.s clan_program.s clang_opt_program.s
CC = clang
CFLAGS = -O0 -g -std=c11 -Wall -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer
# all: clang_program.s clang_opt_program.s

golden/prog1_trace.txt: lexer golden/prog1.c
	./lexer golden/prog1.c > golden/prog1_trace.txt
	git --no-pager diff --color-words golden/prog1_trace.txt

lexer: gen_lexer.c tokens.h common.h
	$(CC) $(CFLAGS) $< -o $@

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
	rm -f *.s *.o driver lexer kuicc a.out

