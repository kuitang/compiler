# all: kuicc_program.s clan_program.s clang_opt_program.s
all: clang_program.s clang_opt_program.s

run: driver
	./driver

kuicc: kuicc.c
	clang -O0 -g -Werror -Wall -pedantic -std=c11 -o kuicc kuicc.c

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

