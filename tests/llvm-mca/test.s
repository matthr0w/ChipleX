	.file	"test.c"
	.option nopic
	.attribute arch, "rv64i2p1_m2p0_a2p1_f2p2_d2p2_c2p0_zicsr2p0_zifencei2p0_zmmul1p0_zaamo1p0_zalrsc1p0_zca1p0_zcd1p0"
	.attribute unaligned_access, 0
	.attribute stack_align, 16
	.text
	.align	1
	.globl	initialize_array
	.type	initialize_array, @function
initialize_array:
	addi	sp,sp,-48
	sd	ra,40(sp)
	sd	s0,32(sp)
	addi	s0,sp,48
	sd	a0,-40(s0)
	mv	a5,a1
	sw	a5,-44(s0)
	sw	zero,-20(s0)
	j	.L2
.L3:
	call	rand
	mv	a5,a0
	mv	a3,a5
	lw	a5,-20(s0)
	slli	a5,a5,2
	ld	a4,-40(s0)
	add	a4,a4,a5
	mv	a5,a3
	sext.w	a2,a5
	li	a3,1374388224
	addi	a3,a3,1311
	mul	a3,a2,a3
	srli	a3,a3,32
	sraiw	a3,a3,5
	mv	a2,a3
	sraiw	a3,a5,31
	subw	a3,a2,a3
	mv	a2,a3
	li	a3,100
	mulw	a3,a2,a3
	subw	a5,a5,a3
	sext.w	a5,a5
	sw	a5,0(a4)
	lw	a5,-20(s0)
	addiw	a5,a5,1
	sw	a5,-20(s0)
.L2:
	lw	a5,-20(s0)
	mv	a4,a5
	lw	a5,-44(s0)
	sext.w	a4,a4
	sext.w	a5,a5
	blt	a4,a5,.L3
	nop
	nop
	ld	ra,40(sp)
	ld	s0,32(sp)
	addi	sp,sp,48
	jr	ra
	.size	initialize_array, .-initialize_array
	.align	1
	.globl	main
	.type	main, @function
main:
	addi	sp,sp,-16
	sd	ra,8(sp)
	sd	s0,0(sp)
	addi	s0,sp,16
	li	t0,-12288
	addi	t0,t0,272
	add	sp,sp,t0
	li	a0,0
	call	time
	mv	a5,a0
	sext.w	a5,a5
	mv	a0,a5
	call	srand
	li	a5,-4096
	addi	a5,a5,88
	addi	a5,a5,-16
	add	a5,a5,s0
	li	a1,1000
	mv	a0,a5
	call	initialize_array
	li	a5,-8192
	addi	a5,a5,184
	addi	a5,a5,-16
	add	a5,a5,s0
	li	a1,1000
	mv	a0,a5
	call	initialize_array
 #APP
# 22 "test.c" 1
	# LLVM-MCA-BEGIN TEST
# 0 "" 2
 #NO_APP
	sw	zero,-20(s0)
	j	.L5
.L6:
	li	a5,-4096
	addi	a5,a5,-16
	add	a4,a5,s0
	lw	a5,-20(s0)
	slli	a5,a5,2
	add	a5,a4,a5
	lw	a4,88(a5)
	li	a5,-8192
	addi	a5,a5,-16
	add	a3,a5,s0
	lw	a5,-20(s0)
	slli	a5,a5,2
	add	a5,a3,a5
	lw	a5,184(a5)
	addw	a5,a4,a5
	sext.w	a4,a5
	li	a5,-12288
	addi	a5,a5,-16
	add	a3,a5,s0
	lw	a5,-20(s0)
	slli	a5,a5,2
	add	a5,a3,a5
	sw	a4,280(a5)
	lw	a5,-20(s0)
	addiw	a5,a5,1
	sw	a5,-20(s0)
.L5:
	lw	a5,-20(s0)
	sext.w	a4,a5
	li	a5,999
	ble	a4,a5,.L6
 #APP
# 25 "test.c" 1
	# LLVM-MCA-END
# 0 "" 2
 #NO_APP
	li	a5,0
	mv	a0,a5
	li	t0,12288
	addi	t0,t0,-272
	add	sp,sp,t0
	ld	ra,8(sp)
	ld	s0,0(sp)
	addi	sp,sp,16
	jr	ra
	.size	main, .-main
	.ident	"GCC: () 15.1.0"
	.section	.note.GNU-stack,"",@progbits
