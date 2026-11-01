/* Demarrage du BIOS de test, sans code Capcom.

   Reproduit, dans le meme ordre, les ecritures de registres faites par le
   boot d'un BIOS de cartouche CPS3 (relevees sous MAME) :
     1. cache coupe ; une petite routine est copiee dans le cache
        (0xC0000000, etiquettes 0x410 en 0x40000000) et executee depuis le
        cache : elle ecrit 0xBD0F en 0xFFFFFE80 et 0x0002 en 0xFFFFFE90
        (registres du SH-2 de la cartouche, a ne pas modifier en executant
        depuis la ROM) ;
     2. bus : BCR1 = 0x0020, BCR2 = 0x00E8, WCR = 0xAA57, puis cache actif ;
     3. DMA canal 0 : 3 mots de 0x1FF00 (bruts) vers 0x27FF0000
        (configuration de la puce de securite) ;
     4. saut a _start (crt0.S), puis hw_init() dans simmtest.c pour le reste
        (video, acquittements, effacements). */

	.section .boot.vectors,"ax"
	.global _vectors
_vectors:
	.long	_boot		/* 0 : reset, PC */
	.long	0x02080000	/* 1 : reset, SP */
	.long	_boot		/* 2 : reset manuel, PC */
	.long	0x02080000	/* 3 : reset manuel, SP */
	.rept	60
	.long	_exc_entry	/* 4-63 : exceptions */
	.endr

	.section .boot.code,"ax"
	.global _boot
_boot:
	mov.l	l_sp,r15
	mov.l	l_ccr,r1
	mov	#0,r0
	mov.w	r0,@r1		/* CCR = 0 : cache coupe */

	/* copie de 0x510-0x54F dans le cache, 4 mots puis une etiquette */
	mov.l	l_src,r1
	mov.l	l_cdata,r2
	mov.l	l_caddr,r3
	mov.l	l_tag,r6
	mov	#4,r5
1:	mov.l	@r1+,r0
	mov.l	r0,@r2
	mov.l	@r1+,r0
	mov.l	r0,@(4,r2)
	mov.l	@r1+,r0
	mov.l	r0,@(8,r2)
	mov.l	@r1+,r0
	mov.l	r0,@(12,r2)
	mov.l	r6,@r3
	add	#16,r2
	dt	r5
	bf/s	1b
	add	#16,r3
	mov.l	l_cdata,r0
	jmp	@r0
	nop

	.align 2
l_sp:	.long	0x02080000
l_ccr:	.long	0xfffffe92
l_src:	.long	0x00000510
l_cdata: .long	0xc0000000
l_caddr: .long	0x40000000
l_tag:	.long	0x00000410

/* Routine executee depuis le cache (copiee de 0x510). Uniquement des
   instructions (pas de constantes en memoire) : le chiffrement des
   instructions lues dans le cache differe de celui des donnees. */
	.section .boot.cache,"ax"
	mov	#-2,r0
	shll8	r0
	or	#0x80,r0
	mov	r0,r1		/* r1 = 0xFFFFFE80 */
	mov	#-67,r0
	shll8	r0
	or	#0x0f,r0	/* r0 = 0x....BD0F */
	mov.w	r0,@r1
	add	#16,r1		/* r1 = 0xFFFFFE90 */
	mov	#2,r0
	mov.w	r0,@r1
	mov	#0x55,r0
	shll2	r0
	shll2	r0		/* r0 = 0x550 : _boot2 */
	jmp	@r0
	nop
	.rept	16
	nop
	.endr

	.section .boot.code2,"ax"
	.global _boot2
_boot2:
	mov.l	m_ccr,r1
	mov	#0x10,r0
	mov.b	r0,@r1		/* CCR : purge */
	mov.l	m_bcr1,r1
	mov.l	m_v1,r0
	mov.l	r0,@r1		/* BCR1 */
	mov.l	m_v2,r0
	mov.l	r0,@(4,r1)	/* BCR2 */
	mov.l	m_v3,r0
	mov.l	r0,@(8,r1)	/* WCR */
	mov.l	m_ccr,r1
	mov	#0x11,r0
	mov.b	r0,@r1		/* CCR : purge + cache actif */

	/* DMA canal 0 : 0x1FF00 -> 0x27FF0000, 3 mots */
	mov.l	m_sar,r1
	mov.l	m_k,r0
	mov.l	r0,@r1		/* SAR0 */
	mov.l	m_sec,r0
	mov.l	r0,@(4,r1)	/* DAR0 */
	mov	#3,r0
	mov.l	r0,@(8,r1)	/* TCR0 */
	mov.l	m_chcr,r0
	mov.l	r0,@(12,r1)	/* CHCR0 */
	mov.l	m_drcr,r2
	mov	#0,r0
	mov.b	r0,@r2		/* DRCR0 */
	mov.l	m_dmaor,r2
	mov	#1,r0
	mov.l	r0,@r2		/* DMAOR : DMA actif */
2:	mov.l	@(12,r1),r0
	tst	#2,r0		/* TE : transfert fini */
	bt	2b
	mov	#0,r0
	mov.l	r0,@r2		/* DMAOR = 0 */

	mov.l	m_start,r0
	jmp	@r0
	nop

	.align 2
m_ccr:	.long	0xfffffe92
m_bcr1:	.long	0xffffffe0
m_v1:	.long	0xa55a0020
m_v2:	.long	0xa55a00e8
m_v3:	.long	0xa55aaa57
m_sar:	.long	0xffffff80
m_k:	.long	0x0001ff00
m_sec:	.long	0x27ff0000
m_chcr:	.long	0x00005a41
m_drcr:	.long	0xfffffe71
m_dmaor: .long	0xffffffb0
m_start: .long	_start

/* Constantes lues par DMA (donc jamais dechiffrees) : laissees en clair
   dans l'image. 0x1FF00 : configuration de la puce de securite ;
   0x1FF0C : encadrement des acces flash ; 0x1FF1C : commandes flash. */
	.section .boot.consts,"a"
	.long	0x03000004, 0xb6010038, 0xe0000000
	.long	0x01050003, 0x01050000, 0x00000001, 0x00000000
	.long	0xaaaaaaaa, 0x55555555, 0xf0f0f0f0, 0x90909090
	.long	0xa0a0a0a0, 0x80808080, 0x10101010, 0x09090909
	.long	0x0e0e0e0e
