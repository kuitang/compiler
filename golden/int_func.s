# KUI'S COMPILER
	.globl	_my_func
_my_func:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$4, %rsp		# a at -4(%rbp) 
	movl	%edi, -4(%rbp)
	subq	$4, %rsp		# b at -8(%rbp) 
	movl	%esi, -8(%rbp)
	subq	$4, %rsp		# c at -12(%rbp) 
	movl	%edx, -12(%rbp)
	subq	$4, %rsp		# d at -16(%rbp) 
	movl	%ecx, -16(%rbp)
	subq	$4, %rsp		# x at -20(%rbp) 
	subq	$4, %rsp		# y at -24(%rbp) 
	subq	$4, %rsp		# t7 at -28(%rbp) 
	movl	-4(%rbp), %eax		# %eax = a
	cltd
	idivl	-8(%rbp)		# %eax = a / b
	movl	%eax, -28(%rbp)		# t7 = %eax
	movl	%eax, -20(%rbp)		# x = %eax
	subq	$4, %rsp		# t8 at -32(%rbp) 
	movl	-16(%rbp), %eax		# %eax = d
	addl	-12(%rbp), %eax		# %eax = c + d
	movl	%eax, -32(%rbp)		# t8 = %eax
	movl	%eax, -24(%rbp)		# y = %eax
	subq	$4, %rsp		# t9 at -36(%rbp) 
	imull	-20(%rbp), %eax		# %eax = x * y
	movl	%eax, -36(%rbp)		# t9 = %eax
	leave
	retq
	leave
	retq
