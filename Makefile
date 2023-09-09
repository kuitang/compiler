# all: kuicc_program.s clan_program.s clang_opt_program.s
CC = clang
CFLAGS = -O0 -g -Werror -Wall -pedantic -std=c11
all: clang_program.s clang_opt_program.s

run: driver
	./driver

gen_tokens: gen_tokens.c
	$(CC) $(CFLAGS) $< -o $@

kuicc: kuicc.c
	$(CC) $(CFLAGS) $< -o $@

# driver: kuicc_program.s driver.c
# 	clang kuicc_program.s driver.c

driver: clang_program.s driver.c
	clang clang_program.s driver.c -o driver

program.s: kuicc program.c
	./kuicc < program.c > program.s

clang_program.s: program.c
	clang -S program.c -o clang_program.s

clang_opt_program.s: program.c
	clang -O3 -S program.c -o clang_opt_program.s


.PHONY: clean run
clean:
	rm -f *.s *.o driver kuicc a.out

