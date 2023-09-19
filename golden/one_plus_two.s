# COMPILED BY KUI AND NOT CLANG!
	.globl	_f
_f:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	$1			# at %rsp = -8(%rbp)
	pushq	$2			# at %rsp = -16(%rbp)
	movq	-8(%rbp), %rax
	addq	-16(%rbp), %rax
	pushq	%rax			# at %rsp = -24(%rbp)

	pushq	$3			# at %rsp = -32(%rbp)
	movq	-24(%rbp), %rax
	addq	-32(%rbp), %rax
	pushq	%rax			# at %rsp = -40(%rbp)

	pushq	$4			# at %rsp = -48(%rbp)
	movq	-40(%rbp), %rax
	subq	-48(%rbp), %rax
	pushq	%rax			# at %rsp = -56(%rbp)

	leave
	retq
