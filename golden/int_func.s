# KUI'S COMPILER at Thu Nov  2 21:01:17 2023
	.globl	_my_func
_my_func:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$4, %rsp		# alloc a (4 bytes) at -4(%rbp) 
	movl	%edi, -4(%rbp)
	subq	$4, %rsp		# alloc b (4 bytes) at -8(%rbp) 
	movl	%esi, -8(%rbp)
	subq	$4, %rsp		# alloc c (4 bytes) at -12(%rbp) 
	movl	%edx, -12(%rbp)
	subq	$4, %rsp		# alloc d (4 bytes) at -16(%rbp) 
	movl	%ecx, -16(%rbp)
# golden/int_func.c:2
	subq	$4, %rsp		# alloc x (4 bytes) at -20(%rbp) 
# golden/int_func.c:3
	subq	$4, %rsp		# alloc y (4 bytes) at -24(%rbp) 
# golden/int_func.c:4
	movl	-4(%rbp), %eax		# %eax = a
	cdq
	idivl	-8(%rbp)		# %eax = a / b
	subq	$4, %rsp		# alloc t7 (4 bytes) at -28(%rbp) 
	movl	%eax, -28(%rbp)		# t7 <-28(%rbp)> = %eax
	movl	-28(%rbp), %edi
	movl	%edi, -20(%rbp)		# x = t7
# golden/int_func.c:5
	movl	-12(%rbp), %eax		# %eax = c
	addl	-16(%rbp), %eax		# %eax = c + d
	subq	$4, %rsp		# alloc t8 (4 bytes) at -32(%rbp) 
	movl	%eax, -32(%rbp)		# t8 <-32(%rbp)> = %eax
	movl	-32(%rbp), %edi
	movl	%edi, -24(%rbp)		# y = t8
# golden/int_func.c:6
	movl	-20(%rbp), %eax		# %eax = x
	imull	-24(%rbp), %eax		# %eax = x * y
	subq	$4, %rsp		# alloc t9 (4 bytes) at -36(%rbp) 
	movl	%eax, -36(%rbp)		# t9 <-36(%rbp)> = %eax
	leave
	retq
	leave
	retq
