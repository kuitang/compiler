# KUI'S COMPILER at Fri Nov  3 23:27:13 2023
	.globl	_f
_f:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$4, %rsp		# alloc x (4 bytes) at -4(%rbp) 
	movl	%edi, -4(%rbp)
	subq	$4, %rsp		# alloc y (4 bytes) at -8(%rbp) 
	movl	%esi, -8(%rbp)
# golden/structs.c:2
# golden/structs.c:9
	subq	$16, %rsp		# alloc a (16 bytes) at -24(%rbp) 
# golden/structs.c:10
	subq	$16, %rsp		# alloc b (16 bytes) at -40(%rbp) 
# golden/structs.c:11
	movl	-4(%rbp), %edi
	movl	%edi, -24(%rbp)		# a.x = x
# golden/structs.c:12
	movl	-8(%rbp), %edi
	movl	%edi, -20(%rbp)		# a.y = y
# golden/structs.c:13
	movl	$10, -16(%rbp)		# a.u = $10
# golden/structs.c:14
	movl	$20, -12(%rbp)		# a.v = $20
# golden/structs.c:16
	movl	$3, %eax		# %eax = $3
	imull	-8(%rbp), %eax		# %eax = $3 * y
	subq	$4, %rsp		# alloc t5 (4 bytes) at -44(%rbp) 
	movl	%eax, -44(%rbp)		# t5 <-44(%rbp)> = %eax
	movl	-44(%rbp), %edi
	movl	%edi, -40(%rbp)		# b.x = t5
# golden/structs.c:17
	movl	$5, %eax		# %eax = $5
	imull	-4(%rbp), %eax		# %eax = $5 * x
	subq	$4, %rsp		# alloc t6 (4 bytes) at -48(%rbp) 
	movl	%eax, -48(%rbp)		# t6 <-48(%rbp)> = %eax
	movl	-48(%rbp), %edi
	movl	%edi, -36(%rbp)		# b.y = t6
# golden/structs.c:18
	movl	$100, -32(%rbp)		# b.u = $100
# golden/structs.c:19
	movl	$200, -32(%rbp)		# b.u = $200
# golden/structs.c:21
	movl	-24(%rbp), %eax		# %eax = a.x
	addl	-20(%rbp), %eax		# %eax = a.x + a.y
	subq	$4, %rsp		# alloc t7 (4 bytes) at -52(%rbp) 
	movl	%eax, -52(%rbp)		# t7 <-52(%rbp)> = %eax
	addl	-16(%rbp), %eax		# %eax = t7 + a.u
	subq	$4, %rsp		# alloc t8 (4 bytes) at -56(%rbp) 
	movl	%eax, -56(%rbp)		# t8 <-56(%rbp)> = %eax
	addl	-12(%rbp), %eax		# %eax = t8 + a.v
	subq	$4, %rsp		# alloc t9 (4 bytes) at -60(%rbp) 
	movl	%eax, -60(%rbp)		# t9 <-60(%rbp)> = %eax
	addl	-40(%rbp), %eax		# %eax = t9 + b.x
	subq	$4, %rsp		# alloc t10 (4 bytes) at -64(%rbp) 
	movl	%eax, -64(%rbp)		# t10 <-64(%rbp)> = %eax
	addl	-36(%rbp), %eax		# %eax = t10 + b.y
	subq	$4, %rsp		# alloc t11 (4 bytes) at -68(%rbp) 
	movl	%eax, -68(%rbp)		# t11 <-68(%rbp)> = %eax
	addl	-32(%rbp), %eax		# %eax = t11 + b.u
	subq	$4, %rsp		# alloc t12 (4 bytes) at -72(%rbp) 
	movl	%eax, -72(%rbp)		# t12 <-72(%rbp)> = %eax
	addl	-28(%rbp), %eax		# %eax = t12 + b.v
	subq	$4, %rsp		# alloc t13 (4 bytes) at -76(%rbp) 
	movl	%eax, -76(%rbp)		# t13 <-76(%rbp)> = %eax
	leave
	retq
	leave
	retq
