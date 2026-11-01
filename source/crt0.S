/* Point d'entree, appele par la fin du boot d'origine (0x498 -> rts vers ici).
   Le boot d'origine a deja : regle le bus, efface les RAM, initialise la video. */
	.section .text.start,"ax"
	.global _start
_start:
	mov.l	stack_top,r15
	mov.l	sr_init,r0
	ldc	r0,sr		/* interruptions masquees (niveau 15) */
	/* efface le .bss */
	mov.l	bss_start,r1
	mov.l	bss_end,r2
	mov	#0,r0
1:	cmp/hs	r2,r1
	bt	2f
	mov.l	r0,@r1
	bra	1b
	add	#4,r1
2:	mov.l	main_addr,r0
	jsr	@r0
	nop
3:	bra	3b
	nop

/* Toutes les exceptions (instruction illegale, erreur d'adresse...) arrivent ici */
	.global _exc_entry
_exc_entry:
	mov.l	stack_top,r15
	mov.l	sr_init,r0
	ldc	r0,sr
	mov.l	exc_addr,r0
	jmp	@r0
	nop

	.align 2
stack_top:	.long 0x02080000
sr_init:	.long 0x000000f0
bss_start:	.long __bss_start
bss_end:	.long __bss_end
main_addr:	.long _main
exc_addr:	.long _exception
