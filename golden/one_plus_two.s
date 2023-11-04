# KUI'S COMPILER at Thu Nov  2 21:01:17 2023
	.globl	_f
_f:
	pushq	%rbp
	movq	%rsp, %rbp
# golden/one_plus_two.c:2
	movl	$1, %eax		# %eax = $1
	addl	$2, %eax		# %eax = $1 + $2
	subq	$4, %rsp		# alloc t1 (4 bytes) at -4(%rbp) 
	movl	%eax, -4(%rbp)		# t1 <-4(%rbp)> = %eax
	addl	$3, %eax		# %eax = t1 + $3
	subq	$4, %rsp		# alloc t2 (4 bytes) at -8(%rbp) 
	movl	%eax, -8(%rbp)		# t2 <-8(%rbp)> = %eax
	subl	$4, %eax		# %eax = t2 - $4
	subq	$4, %rsp		# alloc t3 (4 bytes) at -12(%rbp) 
	movl	%eax, -12(%rbp)		# t3 <-12(%rbp)> = %eax
	leave
	retq
	leave
	retq
