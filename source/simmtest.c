/*
 * CPS3 - BIOS de test des SIMM flash (programme et graphiques)
 *
 * Remplace le programme principal d'un BIOS de cartouche. Le boot d'origine
 * (reglage du bus, effacement des RAM, video) est conserve et saute ici.
 *
 * Acces aux flashs : sequence relevee dans le BIOS sfiii3_euro sous MAME.
 *  - SIMM 1/2 (0x06000000 / 0x06800000) : 4 puces 29F016, une par octet du
 *    mot de 32 bits ; commandes, donnees et lectures passent par le DMA du
 *    SH-2 (le DMA n'est pas soumis au chiffrement de la cartouche).
 *  - SIMM 3 a 7 : 8 puces, fenetre 0x04200000 de 2 Mo choisie par le registre
 *    de banque 0x040C0088 ; une banque = 2 puces (paire) x 1 Mo.
 *    Mot lu : b31-24 puce paire[2n], b23-16 puce impaire[2n],
 *             b15-8 puce paire[2n+1], b7-0 puce impaire[2n+1].
 *  - Encadrement : 0x07FF000C = 0x01050003 (debut) / 0x01050000 (fin), et pour
 *    les SIMM graphiques 0x07FF0048 = 1 pendant les ecritures, 0 sinon.
 *    Ces valeurs sont copiees par DMA depuis 0x1FF0C-0x1FF37 du BIOS (en clair).
 */

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef int s32;

#define R32(a) (*(volatile u32 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R8(a)  (*(volatile u8 *)(a))

#define NC(p) ((u32)(p) | 0x20000000)   /* adresse sans cache */

/* constantes en clair du BIOS, envoyees par DMA comme le BIOS d'origine */
#define K_SEC_ON   0x1ff0c
#define K_SEC_OFF  0x1ff10
#define K_WE_ON    0x1ff14
#define K_WE_OFF   0x1ff18
#define K_AA       0x1ff1c
#define K_55       0x1ff20
#define K_F0       0x1ff24
#define K_90       0x1ff28
#define K_A0       0x1ff2c
#define K_80       0x1ff30
#define K_10       0x1ff34

#define SEC_CTRL   0x27ff000c
#define SEC_WE     0x27ff0048

#define INPUTS     0x25000000
#define SSRAM      0x25040000
#define SSREG      0x25050000
#define PALRAM     0x24080000
#define SPRRAM     0x24000000
#define SPRDMA     0x240c0082
#define GFXWIN     0x24200000
#define GFXBANK    0x240c0088
#define PPUREG     0x240c0000

#define CHIP_SIZE  0x200000

/* boutons (actifs a 0 sur le port, inverses ici) */
#define B_UP    0x00001
#define B_DOWN  0x00002
#define B_LEFT  0x00004
#define B_RIGHT 0x00008
#define B_1     0x00010
#define B_2     0x00020
#define B_3     0x00040
#define B_TEST  0x20000

void *memset(void *d, int c, unsigned n)
{
	u8 *p = d;
	while (n--)
		*p++ = c;
	return d;
}

void *memcpy(void *d, const void *s, unsigned n)
{
	u8 *p = d;
	const u8 *q = s;
	while (n--)
		*p++ = *q++;
	return d;
}

/* ------------------------------------------------------------------ */
/* Temps : FRT du SH-2 a phi/128 (5,12 us par pas a 25 MHz)            */

static u32 t_acc, t_last;

static void timer_init(void)
{
	R8(0xfffffe10) = 0;     /* TIER : pas d'interruption */
	R8(0xfffffe11) = 0;     /* FTCSR */
	R8(0xfffffe16) = 2;     /* TCR : phi/128 */
	R8(0xfffffe12) = 0;
	R8(0xfffffe13) = 0;
	t_acc = 0;
	t_last = 0;
}

static u32 ticks(void)
{
	u32 h = R8(0xfffffe12);
	u32 l = R8(0xfffffe13);
	u32 now = (h << 8) | l;
	t_acc += (now - t_last) & 0xffff;
	t_last = now;
	return t_acc;
}

#define MS(x) ((x) * 195u)      /* 1 ms ~ 195 pas */

/* Attente sans aucun acces au bus externe (timer interne du SH-2, variables
   en registres, boucle dans le cache). Sur vraie carte, un processeur qui
   lit les commandes en boucle sur le bus 0x05xxxxxx perturbe la lecture de
   la RAM SS par la video : texte abime (propre a l'extinction, quand le
   processeur s'arrete). Le BIOS d'origine ne lit les commandes qu'une fois
   par image. */
static void idle_ms(u32 ms)
{
	u32 last = (R8(0xfffffe12) << 8) | R8(0xfffffe13);
	u32 acc = 0;
	while (acc < MS(ms)) {
		u32 now = (R8(0xfffffe12) << 8) | R8(0xfffffe13);
		acc += (now - last) & 0xffff;
		last = now;
	}
}

static void delay_ms(u32 ms)
{
	u32 t0 = ticks();
	while (ticks() - t0 < MS(ms))
		;
}

/* ------------------------------------------------------------------ */
/* DMA canal 0, copie de la routine 0x11C08 du BIOS                     */

static void dma(u32 src, u32 dst, u32 count, u32 size)
{
	u32 v;
	v = R32(0xffffffb0); R32(0xffffffb0) = 0;
	v = R32(0xffffff9c); R32(0xffffff9c) = 0;
	(void)v;
	R32(0xffffff80) = src;
	R32(0xffffff84) = dst;
	R32(0xffffff88) = count & 0xffffff;
	R8(0xfffffe71) = 0;
	v = R32(0xffffff8c);
	R32(0xffffff8c) = 0x5241 | ((size & 3) << 10);
	R32(0xffffffb0) = 1;
	while ((R32(0xffffff8c) & 2) == 0)
		;
	v = R32(0xffffffb0); R32(0xffffffb0) = 0;
}

static u32 dma_word;              /* tampon d'un mot pour les acces DMA */
static u32 dma_buf[1024];         /* 4 Ko */

static void sec_on(void)  { dma(K_SEC_ON,  SEC_CTRL, 1, 2); }
static void sec_off(void) { dma(K_SEC_OFF, SEC_CTRL, 1, 2); }
static void we_on(void)   { dma(K_WE_ON,   SEC_WE,   1, 2); }
static void we_off(void)  { dma(K_WE_OFF,  SEC_WE,   1, 2); }

/* ------------------------------------------------------------------ */
/* Ecran : couche texte "SS" 64x32 cases de 8x8, 48x28 visibles         */

#include "font.h"     /* u8 font8x8[256][8], bit 0 = pixel de gauche */

#define COLS 48
#define ROWS 28

enum { C_WHITE, C_YELLOW, C_RED, C_GREEN, C_CYAN, C_GREY, C_ORANGE, C_BLUE };
static const u16 pal_colors[8] = {
	0x7fff, 0x03ff, 0x001f, 0x03e0, 0x7fe0, 0x4210, 0x021f, 0x7d08
};

/* Sur vraie carte, des ecritures en RAM SS se perdent (acces concurrent de
   la video ?) : texte et police abimes. Chaque octet est donc relu et
   reecrit jusqu'a ce qu'il soit bon, et une copie en RAM principale sert a
   reparer en continu (ss_repair, appele pendant les attentes). */
#define SS_SHADOW 0x6000          /* tuiles, defilement par ligne, police */
static u8 ss_shadow[SS_SHADOW];
static u32 ss_fixes;              /* octets repares (diagnostic) */

static void ss_put(u32 n, u32 v)
{
	volatile u16 *p = (volatile u16 *)(SSRAM + (n >> 1) * 4 + (n & 1) * 2);
	int i;
	v &= 0xff;
	for (i = 0; i < 64; i++) {
		*p = v;
		if ((*p & 0xff) == v && (*p & 0xff) == v)
			return;
		ss_fixes++;
	}
}

static void ss_w(u32 n, u32 v)   /* octet n de la RAM SS */
{
	if (n < SS_SHADOW)
		ss_shadow[n] = v;
	ss_put(n, v);
}

static void ss_repair(void)
{
	static u32 pos;
	int i;
	for (i = 0; i < 256; i++) {
		u32 n = pos;
		volatile u16 *p = (volatile u16 *)(SSRAM + (n >> 1) * 4 + (n & 1) * 2);
		if ((*p & 0xff) != ss_shadow[n])
			ss_put(n, ss_shadow[n]);
		pos = pos + 1 < SS_SHADOW ? pos + 1 : 0;
	}
}

static void putch(int x, int y, u32 ch, int col)
{
	u32 e, v;
	if (x < 0 || x >= 64 || y < 0 || y >= 32)
		return;
	e = y * 64 + x;
	v = (ch & 0xff) | ((col & 31) << 9);
	ss_w(e * 2, v & 0xff);
	ss_w(e * 2 + 1, v >> 8);
}

static void cls(void)
{
	u32 e;
	for (e = 0; e < 64 * 64; e++) {
		ss_w(e * 2, 0x20);
		ss_w(e * 2 + 1, 0);
	}
	R16(SPRDMA) = 9;      /* recopie la liste de sprites vide */
	R16(SPRDMA) = 8;
}

static int cur_x, cur_y, cur_col;

static void at(int x, int y) { cur_x = x; cur_y = y; }
static void color(int c) { cur_col = c; }

/* texte UTF-8 (accents latin-1 seulement) */
static void puts_(const char *s)
{
	while (*s) {
		u32 c = (u8)*s++;
		if (c == '\n') {
			cur_x = 1;
			cur_y++;
			continue;
		}
		if ((c & 0xe0) == 0xc0 && *s) {
			c = ((c & 0x1f) << 6) | ((u8)*s++ & 0x3f);
			if (c > 0xff)
				c = '?';
		}
		putch(cur_x++, cur_y, c, cur_col);
	}
}

static void print_at(int x, int y, int col, const char *s)
{
	at(x, y);
	color(col);
	puts_(s);
}

static void clear_line(int y)
{
	int x;
	for (x = 0; x < COLS; x++)
		putch(x, y, ' ', 0);
}

static const char hexd[] = "0123456789ABCDEF";

static void puthex(u32 v, int digits)
{
	char b[9];
	int i;
	for (i = digits - 1; i >= 0; i--) {
		b[i] = hexd[v & 15];
		v >>= 4;
	}
	b[digits] = 0;
	puts_(b);
}

/* division entiere sans libgcc */
static u32 udiv(u32 n, u32 d, u32 *rem)
{
	u32 q = 0, bit = 1;
	if (d == 0)
		return 0;
	while (d < n && !(d & 0x80000000)) {
		d <<= 1;
		bit <<= 1;
	}
	while (bit) {
		if (n >= d) {
			n -= d;
			q |= bit;
		}
		d >>= 1;
		bit >>= 1;
	}
	if (rem)
		*rem = n;
	return q;
}

static void putdec(u32 v)
{
	char b[12];
	int i = 11;
	b[i] = 0;
	do {
		u32 r;
		v = udiv(v, 10, &r);
		b[--i] = '0' + r;
	} while (v);
	puts_(&b[i]);
}

static u32 percent(u32 a, u32 b)
{
	/* a <= b <= 2^24 */
	if (b == 0)
		return 100;
	return udiv(a * 100, b, 0);
}

/* Fin du demarrage (voir boot.S) : memes ecritures que le boot d'origine,
   dans le meme ordre et avec la meme largeur d'acces. */
#include "hw_tab.h"

static void irq_ack(void)
{
	R16(0x05100000) = 0;
	R16(0x05110000) = 0;
	R16(0x05120000) = 0;
	R16(0x05130000) = 0;
	R16(0x05000c10) = 0;
	R16(0x05000c12) = 0;
	R16(0x05000c14) = 0;
	R16(0x05000c16) = 0;
	R16(0x05000c18) = 0;
	R16(0x05000c1a) = 0;
	R16(0x05150002) = 0x4642;
}

/* Effacement verifie d'une zone video (adresses non cachees 0x2xxxxxxx) :
   ecrit, relit, recommence tant que des mots ne sont pas a la valeur.
   Retourne le nombre de mots trouves faux apres la premiere ecriture. */
static u32 pal_fixes, spr_fixes;
static u32 clear_check(u32 base, u32 len, u32 val)
{
	u32 x, bad, tot = 0;
	int pass;
	for (x = 0; x < len; x += 4)
		R32(base + x) = val;
	for (pass = 0; pass < 16; pass++) {
		bad = 0;
		for (x = 0; x < len; x += 4)
			if (R32(base + x) != val) {
				R32(base + x) = val;
				bad++;
			}
		tot += bad;
		if (!bad)
			break;
	}
	return tot;
}

static void hw_init(void)
{
	u32 i;
	for (i = 0; i < sizeof(hw_tab) / sizeof(hw_tab[0]); i++) {
		if (hw_tab[i].n == 1)
			R8(hw_tab[i].a) = hw_tab[i].v;
		else
			R16(hw_tab[i].a) = hw_tab[i].v;
	}
	irq_ack();
	/* Le boot d'origine teste la RAM principale entre ces reglages et les
	   effacements (~7 images, 120 ms) : sur vraie carte, la video n'accepte
	   pas encore les ecritures juste apres l'allumage (fond non efface). */
	idle_ms(300);
	/* RAM des sprites a zero (verifiee), liste minimale, DMA de liste */
	spr_fixes += clear_check(0x24000000, 0x7e000, 0);
	R32(0x04000000) = 0x00010200;
	R32(0x04000004) = 0;
	R32(0x04002000) = 0;
	R32(0x04002004) = 0x03000000;
	R16(0x040c0082) = 8;
	for (i = 0; i < 100000 && (R16(0x040c000c) & 1); i++)
		;
	irq_ack();
}

static void video_init(void)
{
	u32 t, r, x;

	/* Une vraie carte demarre avec des RAM video au contenu aleatoire (MAME
	   les met a zero). Comme le BIOS d'origine : palette entiere a 0 (tout
	   ce que les autres couches pourraient afficher devient noir) et RAM SS
	   entiere a 0 (carte de tuiles, defilement par ligne, police). */
	pal_fixes += clear_check(PALRAM, 0x40000, 0);
	for (x = 0; x < 0x8000; x++)
		ss_w(x, 0);
	/* liste de sprites vide (bit 31 = fin de liste), recopiee par le DMA
	   de liste (front 9 -> 8) : aucun sprite ni plan de tuiles */
	for (x = 0; x < 0x2000; x += 4)
		R32(SPRRAM + x) = 0x80000000;
	R16(SPRDMA) = 9;
	R16(SPRDMA) = 8;

	/* Reglages video faits par le BIOS sfiii3_euro au debut du programme
	   principal (trace MAME), en plus de ceux du boot. */
	for (r = 0; r < 0x20; r += 2)
		R16(PPUREG + r) = 0;
	R16(PPUREG + 0x60) = 0x002a; R16(PPUREG + 0x62) = 0x006f;
	R16(PPUREG + 0x64) = 0x01ef; R16(PPUREG + 0x66) = 0x01c6;
	R16(PPUREG + 0x68) = 0x0000; R16(PPUREG + 0x6a) = 0x0000;
	R16(PPUREG + 0x6c) = 0x03ff; R16(PPUREG + 0x6e) = 0x0040;
	R16(PPUREG + 0x70) = 0x0003; R16(PPUREG + 0x72) = 0x0015;
	R16(PPUREG + 0x74) = 0x00f5; R16(PPUREG + 0x76) = 0x0106;
	R16(PPUREG + 0x78) = 0x0000; R16(PPUREG + 0x7a) = 0x0000;
	R16(PPUREG + 0x7c) = 0x03ff; R16(PPUREG + 0x7e) = 0x0040;
	/* Registre de controle video en 0x040C0080 (ignore par MAME) : le boot
	   y met 0x0020, le BIOS d'origine 0x0063 juste apres ces reglages de
	   balayage (routine 0x13538 de sfiii3_euro). Sans lui, la couche texte
	   s'affiche mal sur vraie carte. */
	R16(PPUREG + 0x80) = 0x0063;

	R16(SSREG + 0x00) = 0x002a; R16(SSREG + 0x02) = 0x003e;
	R16(SSREG + 0x04) = 0x0000; R16(SSREG + 0x06) = 0x0016;
	R16(SSREG + 0x08) = 0x0002; R16(SSREG + 0x0a) = 0x01c6;
	R16(SSREG + 0x0c) = 0x0001;
	R16(SSREG + 0x12) = 0x0003;
	R16(SSREG + 0x14) = 0x0015; R16(SSREG + 0x16) = 0x0000;
	R16(SSREG + 0x18) = 0x00f7; R16(SSREG + 0x1a) = 0x0000;
	R16(SSREG + 0x1c) = 0x0106; R16(SSREG + 0x1e) = 0x0001;
	R16(SSREG + 0x26) = 0x0003;
	/* defilement de la couche texte et base de palette (0xBF) */
	R16(SSREG + 0x0e) = 0xff9b;
	R16(SSREG + 0x10) = 0x00ff;
	R16(SSREG + 0x20) = 0xffe8;
	R16(SSREG + 0x22) = 0x00ff;
	R16(SSREG + 0x28) = 0x0000;
	R16(SSREG + 0x24) = 0x00bf;

	/* palettes de la couche texte : couleur 0x17E00 + pal*16 + encre */
	for (r = 0; r < 32; r++)
		for (x = 0; x < 16; x++)
			R16(PALRAM + 2 * (0x17e00 + r * 16 + x)) =
				x == 0 ? 0 : pal_colors[r & 7];

	/* police : tuile t en RAM SS 0x4000 + t*32, 4 octets par ligne,
	   pixel pair = quartet bas */
	for (t = 0; t < 256; t++)
		for (r = 0; r < 8; r++) {
			u32 bits = font8x8[t][r];
			for (x = 0; x < 4; x++) {
				u32 lo = (bits >> (2 * x)) & 1;
				u32 hi = (bits >> (2 * x + 1)) & 1;
				ss_w(0x4000 + t * 32 + r * 4 + x, lo | (hi << 4));
			}
		}
	/* pas de defilement par ligne */
	for (r = 0; r < 0x400; r++)
		ss_w(0x2000 + r, 0);
	cls();
}

/* ------------------------------------------------------------------ */
/* Manette                                                               */

static u32 pad_raw(void)
{
	return ~R32(INPUTS) & 0x3ffff;
}

/* attend un appui (front montant), avec repetition sur les directions ;
   commandes lues toutes les 16 ms */
static u32 pad_wait(void)
{
	static u32 prev;
	static u32 held_t;
	u32 t0 = ticks();
	for (;;) {
		u32 p, now, edge;
		idle_ms(16);
		p = pad_raw();
		now = ticks();
		edge = p & ~prev;
		if (edge) {
			prev = p;
			held_t = now;
			delay_ms(20);
			return edge;
		}
		if ((p & 0xf) && (p == prev) && now - held_t > MS(350)) {
			held_t = now - MS(250);
			return p & 0xf;
		}
		if (p != prev) {
			prev = p;
			held_t = now;
		}
		(void)t0;
	}
}

static int abort_requested(void)
{
	return (pad_raw() & (B_2 | B_3)) == (B_2 | B_3);
}

/* ------------------------------------------------------------------ */
/* CRC32                                                                 */

static u32 crctab[256];

static void crc_init(void)
{
	u32 i, j, c;
	for (i = 0; i < 256; i++) {
		c = i;
		for (j = 0; j < 8; j++)
			c = (c & 1) ? (c >> 1) ^ 0xedb88320 : c >> 1;
		crctab[i] = c;
	}
}

#define CRC_UPD(c, b) ((c) = crctab[((c) ^ (b)) & 0xff] ^ ((c) >> 8))

/* references (nom MAME du fichier puce + CRC32), generees depuis cps3.cpp */
struct ref { u32 crc; const char *name; };
#include "refs.h"     /* static const struct ref refs[]; NREFS */

static const struct ref *find_ref(u32 crc)
{
	int i;
	for (i = 0; i < NREFS; i++)
		if (refs[i].crc == crc)
			return &refs[i];
	return 0;
}

/* ------------------------------------------------------------------ */
/* Description des SIMM                                                  */

static int slot_is_gfx(int s) { return s >= 3; }
static int slot_chips(int s) { return slot_is_gfx(s) ? 8 : 4; }

static u32 prg_base(int s) { return s == 1 ? 0x26000000 : 0x26800000; }

static void gfx_bank(int s, int pair, int half)
{
	R16(GFXBANK) = (s - 3) * 8 + pair * 2 + half + 2;
}

/* commandes SIMM programme : 4 puces en parallele */
static void p_cmd(u32 base, u32 k)
{
	dma(K_AA, base + 0x1554, 1, 2);
	dma(K_55, base + 0x0aa8, 1, 2);
	dma(k, base + 0x1554, 1, 2);
}

static u32 p_read(u32 addr)
{
	dma(addr, NC(&dma_word), 1, 2);
	return R32(NC(&dma_word));
}

static void p_write(u32 addr, u32 v)
{
	R32(NC(&dma_word)) = v;
	dma(NC(&dma_word), addr, 1, 2);
}

/* commandes SIMM graphique : 2 puces de la banque courante en parallele.
   L'adresse 0x555 des puces = octet impair du mot 0x2AA, 0x2AA = octet
   pair du mot 0x155. */
static void g_cmd(u16 c)
{
	R16(GFXWIN + 0x2aa * 4 + 2) = 0xaaaa;
	R16(GFXWIN + 0x155 * 4 + 0) = 0x5555;
	R16(GFXWIN + 0x2aa * 4 + 2) = c;
}

static void g_cmd_we(int s, int pair, u16 c)
{
	gfx_bank(s, pair, 0);
	we_on();
	g_cmd(c);
	we_off();
}

/* ------------------------------------------------------------------ */
/* Identification                                                        */

struct ident {
	u8 manuf[8];
	u8 dev[8];
	u8 ok[8];
};

static int id_valid(u32 m, u32 d)
{
	return d == 0xad && m != 0xff && m != 0x00;
}

static void identify(int s, struct ident *id)
{
	int c;
	memset(id, 0, sizeof(*id));
	sec_on();
	if (!slot_is_gfx(s)) {
		u32 b = prg_base(s), m, d;
		p_cmd(b, K_F0);
		p_cmd(b, K_90);
		m = p_read(b);
		d = p_read(b + 4);
		p_cmd(b, K_F0);
		for (c = 0; c < 4; c++) {
			id->manuf[c] = m >> (24 - 8 * c);
			id->dev[c] = d >> (24 - 8 * c);
		}
	} else {
		int p;
		for (p = 0; p < 4; p++) {
			u32 w;
			g_cmd_we(s, p, 0xf0f0);
			g_cmd_we(s, p, 0x9090);
			w = R32(GFXWIN);
			g_cmd_we(s, p, 0xf0f0);
			id->manuf[2 * p] = w >> 24;
			id->manuf[2 * p + 1] = w >> 16;
			id->dev[2 * p] = w >> 8;
			id->dev[2 * p + 1] = w;
		}
		R16(GFXBANK) = 0;
	}
	sec_off();
	for (c = 0; c < slot_chips(s); c++)
		id->ok[c] = id_valid(id->manuf[c], id->dev[c]);
}

static const char *manuf_name(u32 m)
{
	switch (m) {
	case 0x04: return "Fujitsu";
	case 0x01: return "AMD";
	case 0x20: return "ST";
	case 0xc2: return "Macronix";
	case 0x1f: return "Atmel";
	case 0x89: return "Intel";
	case 0x37: return "AMIC";
	case 0xad: return "Hyundai";
	case 0x98: return "Toshiba";
	case 0xda: return "Winbond";
	}
	return "?";
}

/* ------------------------------------------------------------------ */
/* Lecture d'une puce entiere, octet par octet, avec un rappel           */
/* Les deux SIMM d'une comparaison sont lues ensemble par blocs.         */

#define BLK 1024   /* mots par bloc */

static u32 cmp_buf[BLK];

/* lit le bloc de mots [w, w+BLK) d'un groupe :
   SIMM programme : mot = 4 puces ; SIMM graphique : paire 'pair'. */
static void read_block(int s, int pair, u32 w, u32 *dst)
{
	int i;
	if (!slot_is_gfx(s)) {
		dma(prg_base(s) + w * 4, NC(dst), BLK, 2);
	} else {
		/* w = index de mot dans la puce (2 octets par puce par mot) */
		u32 half = w >= 0x80000;
		u32 o = w & 0x7ffff;
		volatile u32 *p = (volatile u32 *)(GFXWIN + o * 4);
		u32 *d = (u32 *)NC(dst);
		gfx_bank(s, pair, half);
		for (i = 0; i < BLK; i++)
			d[i] = p[i];
	}
}

/* extrait l'octet d'une puce depuis un mot lu.
   programme : lane = c (0..3), 1 octet par mot ;
   graphique : 2 octets par mot pour chaque puce de la paire. */

/* ------------------------------------------------------------------ */
/* Resultats par puce                                                    */

struct res {
	u32 errors;
	u32 nfirst;
	u32 faddr[3];
	u8 fexp[3], fgot[3];
	u32 bit0[8];     /* attendu 1, lu 0 */
	u32 bit1[8];     /* attendu 0, lu 1 */
	u32 avote[21];
	u32 nvote;
	u32 crc;
	u8 orall, andall;
	u32 nff;
	u8 dead;         /* ne repond plus (delai depasse) */
	u32 timeouts;
	u8 alias[21];    /* lecture seule : ligne d'adresse suspecte */
	u8 erase_fail;
	u8 id_m, id_d, id_bad;
};

static struct res res[8];
static int verify_passes;

static void res_clear(void)
{
	memset(res, 0, sizeof(res));
	{
		int c;
		for (c = 0; c < 8; c++)
			res[c].andall = 0xff;
	}
}

static void note_error(int c, u32 a, u32 exp, u32 got)
{
	struct res *r = &res[c];
	u32 x = exp ^ got, b;
	r->errors++;
	if (r->nfirst < 3) {
		r->faddr[r->nfirst] = a;
		r->fexp[r->nfirst] = exp;
		r->fgot[r->nfirst] = got;
		r->nfirst++;
	}
	for (b = 0; b < 8; b++)
		if (x & (1 << b)) {
			if (exp & (1 << b))
				r->bit0[b]++;
			else
				r->bit1[b]++;
		}
}

/* ------------------------------------------------------------------ */
/* Motif de test : depend de tous les bits d'adresse                    */

static u32 pat(int c, u32 a)
{
	u32 x = a ^ (a >> 11) ^ (a >> 17);
	x *= 0x9e3779b1;
	return ((x >> 24) ^ (c * 0x3b) ^ (a >> 5)) & 0xff;
}

static void vote_alias(int c, u32 a, u32 got, int inv)
{
	struct res *r = &res[c];
	int k;
	if (r->nvote >= 512)
		return;
	r->nvote++;
	for (k = 0; k < 21; k++) {
		u32 p = pat(c, a ^ (1u << k));
		if (inv)
			p ^= 0xff;
		if (p == got)
			r->avote[k]++;
	}
}

/* ------------------------------------------------------------------ */
/* Ecran de progression                                                  */

static u32 prog_last;

static void progress(const char *what, u32 done, u32 total)
{
	u32 p = percent(done, total);
	if (done != 0 && p == prog_last)
		return;
	prog_last = p;
	clear_line(20);
	print_at(2, 20, C_CYAN, what);
	puts_(" : ");
	putdec(p);
	puts_(" %");
}

/* ------------------------------------------------------------------ */
/* Ecran : en-tete commun                                                */

static void title(const char *t)
{
	cls();
	print_at(1, 1, C_YELLOW, "CPS3  TEST DES SIMM FLASH");
	print_at(1, 2, C_GREY, "-----------------------------------------------");
	print_at(1, 3, C_WHITE, t);
}

static void wait_back(void)
{
	print_at(1, 26, C_GREY, "Bouton 1 ou 2 : retour");
	for (;;) {
		u32 k = pad_wait();
		if (k & (B_1 | B_2 | B_TEST))
			return;
	}
}

static void chip_label(int s, int c)
{
	puts_("SIMM");
	putdec(s);
	puts_(".");
	putdec(c);
}

static void lanes_text(int s, int c)
{
	/* bits du bus de la carte portes par la puce */
	if (!slot_is_gfx(s)) {
		static const char *const l[4] = { "D31-24", "D23-16", "D15-8", "D7-0" };
		puts_(l[c]);
	} else {
		puts_((c & 1) ? "D23-16/D7-0" : "D31-24/D15-8");
	}
}

/* ------------------------------------------------------------------ */
/* 1. Identification de toutes les SIMM                                  */

static void screen_ident(void)
{
	int s, c, y = 5;
	title("Identification des puces (commande 90h)");
	print_at(1, 4, C_GREY, "fabricant/composant, 29F016 = xx/AD");
	for (s = 1; s <= 7; s++) {
		struct ident id;
		int n = slot_chips(s), ok = 0;
		identify(s, &id);
		for (c = 0; c < n; c++)
			ok += id.ok[c];
		at(1, y);
		color(C_WHITE);
		puts_("SIMM ");
		putdec(s);
		puts_(slot_is_gfx(s) ? " graph. " : " progr. ");
		if (ok == 0) {
			color(C_GREY);
			puts_("absente ou aucune puce ne repond");
			y++;
			continue;
		}
		color(ok == n ? C_GREEN : C_ORANGE);
		putdec(ok);
		puts_("/");
		putdec(n);
		puts_(" repondent (");
		puts_(manuf_name(id.manuf[0]));
		puts_(")");
		y++;
		at(2, y);
		for (c = 0; c < n; c++) {
			color(C_GREY);
			puts_(".");
			putdec(c);
			puts_(":");
			color(id.ok[c] ? C_GREEN : C_RED);
			puthex(id.manuf[c], 2);
			puts_("/");
			puthex(id.dev[c], 2);
			puts_("  ");
			if (c == 3 && n == 8) {
				y++;
				at(2, y);
			}
		}
		y++;
	}
	print_at(1, 24, C_GREY, "SIMM 32 Mbit : seules .0 et .1 sont montees.");
	wait_back();
}

/* ------------------------------------------------------------------ */
/* Choix d'un slot                                                       */

static int choose_slot(const char *question, int def, int need_type)
{
	int s = def;
	title(question);
	print_at(1, 6, C_GREY, "Gauche/droite : slot   Bouton 1 : valider");
	print_at(1, 7, C_GREY, "Bouton 2 : annuler");
	for (;;) {
		u32 k;
		clear_line(10);
		at(4, 10);
		color(C_YELLOW);
		puts_("<  SIMM ");
		putdec(s);
		puts_(slot_is_gfx(s) ? "  (graphique, 8 puces)  >" :
		      "  (programme, 4 puces)  >");
		/* SIMM presente ? (identification 90h des puces du slot) */
		{
			struct ident id;
			int c, n = slot_chips(s), ok = 0, first = 0;
			identify(s, &id);
			for (c = n - 1; c >= 0; c--)
				if (id.ok[c]) {
					ok++;
					first = c;
				}
			clear_line(12);
			at(4, 12);
			if (ok == 0) {
				color(C_RED);
				puts_("Non detectee : aucune puce ne repond");
			} else {
				color(ok == n ? C_GREEN : C_ORANGE);
				puts_("Detectee : ");
				putdec(ok);
				puts_("/");
				putdec(n);
				puts_(" puces repondent (");
				puts_(manuf_name(id.manuf[first]));
				puts_(")");
			}
		}
		k = pad_wait();
		if (k & (B_LEFT | B_UP)) {
			do {
				s = s == 1 ? 7 : s - 1;
			} while (need_type && slot_is_gfx(s) != slot_is_gfx(need_type));
		}
		if (k & (B_RIGHT | B_DOWN)) {
			do {
				s = s == 7 ? 1 : s + 1;
			} while (need_type && slot_is_gfx(s) != slot_is_gfx(need_type));
		}
		if (k & (B_1 | B_TEST))
			return s;
		if (k & B_2)
			return 0;
	}
}

/* ------------------------------------------------------------------ */
/* Parcours d'une SIMM complete en lecture                               */
/* cb(c, a, byte) pour chaque octet de chaque puce, dans l'ordre des    */
/* adresses de chaque puce.                                              */

/* SIMM programme : bloc de BLK mots = BLK octets par puce.
   SIMM graphique : bloc de BLK mots = 2*BLK octets par puce de la paire. */

static void crc_scan(int s)
{
	u32 w, i;
	int c;
	u32 crc[8], orv[8], andv[8], nff[8];
	for (c = 0; c < 8; c++) {
		crc[c] = 0xffffffff;
		orv[c] = 0;
		andv[c] = 0xff;
		nff[c] = 0;
	}
	if (!slot_is_gfx(s)) {
		for (w = 0; w < CHIP_SIZE; w += BLK) {
			u32 *d = (u32 *)NC(dma_buf);
			progress("Lecture", w, CHIP_SIZE);
			read_block(s, 0, w, dma_buf);
			for (i = 0; i < BLK; i++) {
				u32 v = d[i];
				for (c = 0; c < 4; c++) {
					u32 b = (v >> (24 - 8 * c)) & 0xff;
					CRC_UPD(crc[c], b);
					orv[c] |= b;
					andv[c] &= b;
					nff[c] += (b == 0xff);
				}
			}
		}
	} else {
		int p;
		for (p = 0; p < 4; p++) {
			int c0 = 2 * p, c1 = 2 * p + 1;
			for (w = 0; w < CHIP_SIZE / 2; w += BLK) {
				u32 *d = (u32 *)NC(dma_buf);
				progress("Lecture", p * (CHIP_SIZE / 2) + w, 2 * CHIP_SIZE);
				read_block(s, p, w, dma_buf);
				for (i = 0; i < BLK; i++) {
					u32 v = d[i];
					u32 a0 = (v >> 24) & 0xff, b0 = (v >> 16) & 0xff;
					u32 a1 = (v >> 8) & 0xff, b1 = v & 0xff;
					CRC_UPD(crc[c0], a0);
					CRC_UPD(crc[c0], a1);
					CRC_UPD(crc[c1], b0);
					CRC_UPD(crc[c1], b1);
					orv[c0] |= a0 | a1;
					andv[c0] &= a0 & a1;
					orv[c1] |= b0 | b1;
					andv[c1] &= b0 & b1;
					nff[c0] += (a0 == 0xff) + (a1 == 0xff);
					nff[c1] += (b0 == 0xff) + (b1 == 0xff);
				}
			}
		}
		R16(GFXBANK) = 0;
	}
	for (c = 0; c < 8; c++) {
		res[c].crc = ~crc[c];
		res[c].orall = orv[c];
		res[c].andall = andv[c];
		res[c].nff = nff[c];
	}
}

/* octet a l'adresse a d'une puce (acces direct, lent) */
static u32 read_byte(int s, int c, u32 a)
{
	if (!slot_is_gfx(s)) {
		u32 v = p_read(prg_base(s) + a * 4);
		return (v >> (24 - 8 * c)) & 0xff;
	} else {
		u32 w = a >> 1, v;
		gfx_bank(s, c >> 1, w >= 0x80000);
		v = R32(GFXWIN + (w & 0x7ffff) * 4);
		if (a & 1)
			return (c & 1) ? v & 0xff : (v >> 8) & 0xff;
		return (c & 1) ? (v >> 16) & 0xff : (v >> 24) & 0xff;
	}
}

/* lignes d'adresse qui semblent sans effet : a et a^(1<<k) toujours egaux */
static void alias_scan(int s)
{
	int c, k, n = slot_chips(s);
	for (c = 0; c < n; c++) {
		for (k = 0; k < 21; k++) {
			u32 i, valid = 0, same = 0;
			for (i = 0; i < 160; i++) {
				u32 a = (i * 0x9e3779b1u) >> 11;   /* 21 bits */
				u32 v1, v2;
				a &= ~(1u << k);
				v1 = read_byte(s, c, a);
				v2 = read_byte(s, c, a | (1u << k));
				if ((v1 == 0xff && v2 == 0xff) || (v1 == 0 && v2 == 0))
					continue;
				valid++;
				same += (v1 == v2);
			}
			res[c].alias[k] = (valid >= 40 && same == valid);
		}
	}
	R16(GFXBANK) = 0;
}

static void print_bits(u8 mask)
{
	int b, first = 1;
	for (b = 0; b < 8; b++)
		if (mask & (1 << b)) {
			if (!first)
				puts_(",");
			puts_("D");
			putdec(b);
			first = 0;
		}
}

/* ------------------------------------------------------------------ */
/* 2. Controle du contenu                                                */

static void screen_crc(void)
{
	int s = choose_slot("Controle du contenu (CRC32 par puce)", 3, 0);
	int c, n, y, nok = 0, nknown = 0;
	struct ident id;
	const char *game = 0;
	u32 gamelen = 0;
	if (!s)
		return;
	n = slot_chips(s);
	title("Controle du contenu - lecture en cours");
	res_clear();
	identify(s, &id);
	prog_last = 999;
	crc_scan(s);
	progress("Analyse des lignes d'adresse", 0, 1);
	alias_scan(s);

	/* reference de la premiere puce reconnue : "<set> simmN." */
	for (c = 0; c < n; c++) {
		const struct ref *r = find_ref(res[c].crc);
		if (r && !game) {
			const char *p = r->name;
			game = r->name;
			while (*p && *p != '.')
				p++;
			if (*p == '.')
				gamelen = p + 1 - r->name;
			else
				game = 0;
		}
	}

	title("Controle du contenu");
	at(1, 3);
	color(C_WHITE);
	puts_("Controle du contenu  SIMM ");
	putdec(s);
	y = 5;
	for (c = 0; c < n; c++) {
		const struct ref *r = find_ref(res[c].crc);
		int k;
		at(1, y);
		color(C_GREY);
		puts_(".");
		putdec(c);
		puts_(" ");
		color(C_WHITE);
		puthex(res[c].crc, 8);
		puts_(" ");
		if (res[c].nff == CHIP_SIZE) {
			color(C_CYAN);
			puts_("vierge (tout a FF)");
		} else if (r) {
			color(C_GREEN);
			puts_(r->name);
			nok++;
			nknown++;
		} else {
			color(C_RED);
			puts_("INCONNU");
			if (game) {
				/* CRC attendu pour cette puce dans le meme dump */
				char g[28];
				u32 i;
				const struct ref *e = 0;
				for (i = 0; i < gamelen && i < 24; i++)
					g[i] = game[i];
				g[i++] = '0' + c;
				g[i] = 0;
				for (i = 0; i < NREFS; i++) {
					const char *x = refs[i].name, *y = g;
					while (*x && *x == *y) {
						x++;
						y++;
					}
					if (!*x && !*y) {
						e = &refs[i];
						break;
					}
				}
				if (e) {
					color(C_ORANGE);
					puts_(" attendu ");
					puthex(e->crc, 8);
				}
			}
		}
		y++;
		/* indices materiels */
		at(4, y);
		color(C_ORANGE);
		{
			u8 st0 = ~res[c].orall, st1 = res[c].andall;
			int any = 0;
			if (!id.ok[c]) {
				color(C_RED);
				puts_("ID lu ");
				puthex(id.manuf[c], 2);
				puts_("/");
				puthex(id.dev[c], 2);
				puts_("  ");
				color(C_ORANGE);
				any = 1;
			}
			if (res[c].nff != CHIP_SIZE && st0) {
				puts_("toujours 0 : ");
				print_bits(st0);
				puts_("  ");
				any = 1;
			}
			if (res[c].nff != CHIP_SIZE && st1 && res[c].nff != CHIP_SIZE) {
				puts_("toujours 1 : ");
				print_bits(st1);
				puts_("  ");
				any = 1;
			}
			for (k = 0; k < 21; k++)
				if (res[c].alias[k]) {
					puts_("A");
					putdec(k);
					puts_("? ");
					any = 1;
				}
			if (any)
				y++;
		}
		if (y > 23)
			break;
	}
	at(1, 24);
	if (nknown == n) {
		color(C_GREEN);
		puts_("Toutes les puces correspondent a un dump MAME.");
	} else if (game) {
		color(C_RED);
		puts_("Puce(s) INCONNU(E)S : contenu different du dump.");
		at(1, 25);
		puts_("Faire le test 3 (comparaison) ou 4 (ecriture).");
	} else {
		color(C_ORANGE);
		puts_("Aucun dump reconnu : comparer a une SIMM saine.");
	}
	(void)nok;
	wait_back();
}

/* ------------------------------------------------------------------ */
/* Affichage d'un bilan d'erreurs par puce (comparaison et ecriture)    */

static void show_chip_detail(int s, int c)
{
	struct res *r = &res[c];
	int b, k, y;
	title("Detail");
	at(1, 3);
	color(C_WHITE);
	puts_("Detail ");
	chip_label(s, c);
	puts_("  (bus ");
	lanes_text(s, c);
	puts_(")");
	at(1, 4);
	color(r->id_bad ? C_RED : C_GREY);
	puts_("ID ");
	puthex(r->id_m, 2);
	puts_("/");
	puthex(r->id_d, 2);
	puts_(r->id_bad ? "  (attendu xx/AD : ID faux)" : "  (29F016)");
	at(1, 5);
	if (r->dead) {
		color(C_RED);
		puts_("La puce ne repond plus (delai depasse).");
		at(1, 6);
	}
	if (r->erase_fail) {
		color(C_RED);
		puts_("Effacement en echec (DQ5 / delai)");
		if (r->bit1[5])
			puts_(" ou D5 a 1");
		at(1, 7);
	}
	color(r->errors ? C_RED : C_GREEN);
	at(1, 8);
	puts_("Octets faux : ");
	putdec(r->errors);
	if (verify_passes > 1) {
		puts_(" (");
		putdec(verify_passes);
		puts_(" relectures)");
	} else {
		puts_(" / 2097152");
	}
	y = 10;
	for (k = 0; k < (int)r->nfirst; k++) {
		at(1, y++);
		color(C_WHITE);
		puts_("adr puce ");
		puthex(r->faddr[k], 6);
		puts_("  attendu ");
		puthex(r->fexp[k], 2);
		puts_("  lu ");
		puthex(r->fgot[k], 2);
	}
	y++;
	at(1, y++);
	color(C_GREY);
	puts_("bit  lu 0 (attendu 1)  lu 1 (attendu 0)");
	for (b = 0; b < 8; b++) {
		at(1, y++);
		color((r->bit0[b] || r->bit1[b]) ? C_RED : C_GREEN);
		puts_("D");
		putdec(b);
		at(6, y - 1);
		putdec(r->bit0[b]);
		at(26, y - 1);
		putdec(r->bit1[b]);
	}
	if (r->nvote) {
		at(1, y++);
		color(C_ORANGE);
		puts_("lignes d'adresse suspectes : ");
		for (k = 0; k < 21; k++)
			if (r->avote[k] * 2 > r->nvote) {
				puts_("A");
				putdec(k);
				puts_(" ");
			}
	}
}

/* une ligne par puce : ".5 HS 1311380 err D7..0:....0... A12" */
static void verdict_line(int s, int c, int y)
{
	struct res *r = &res[c];
	int b, k;
	at(1, y);
	color(C_GREY);
	puts_(".");
	putdec(c);
	puts_(" ");
	if (!r->errors && !r->dead && !r->erase_fail) {
		color(C_GREEN);
		puts_("OK");
		if (r->id_bad) {
			color(C_ORANGE);
			puts_("  mais ID lu ");
			puthex(r->id_m, 2);
			puts_("/");
			puthex(r->id_d, 2);
		}
		return;
	}
	color(C_RED);
	puts_(r->dead ? "HS muette " : "HS ");
	if (r->id_bad)
		puts_("ID? ");
	putdec(r->errors);
	puts_(" err ");
	color(C_ORANGE);
	puts_("D7..0:");
	for (b = 7; b >= 0; b--) {
		char t[2] = { '.', 0 };
		if (r->bit0[b] && r->bit1[b])
			t[0] = 'x';
		else if (r->bit0[b])
			t[0] = '0';
		else if (r->bit1[b])
			t[0] = '1';
		puts_(t);
	}
	if (r->nvote)
		for (k = 0; k < 21; k++)
			if (r->avote[k] * 2 > r->nvote) {
				puts_(" A");
				putdec(k);
			}
	(void)s;
}

static void results_screens(int s, const char *what)
{
	int n = slot_chips(s), page = -1, c;
	for (;;) {
		u32 k;
		if (page < 0) {
			int bad = 0;
			title(what);
			at(1, 3);
			color(C_WHITE);
			puts_(what);
			puts_("  SIMM ");
			putdec(s);
			for (c = 0; c < n; c++) {
				verdict_line(s, c, 5 + c);
				if (res[c].errors || res[c].dead || res[c].erase_fail)
					bad++;
			}
			at(1, 15);
			color(bad ? C_RED : C_GREEN);
			if (bad) {
				putdec(bad);
				puts_(" puce(s) en defaut.");
			} else {
				puts_("Aucune erreur.");
			}
			print_at(1, 17, C_GREY, "D7..0 : un caractere par bit de donnee");
			print_at(1, 18, C_GREY, " .  bon     0  lu 0 au lieu de 1");
			print_at(1, 19, C_GREY, " 1  lu 1 au lieu de 0    x  les deux");
			print_at(1, 20, C_GREY, "Ax : ligne d'adresse suspecte");
			print_at(1, 21, C_GREY, "muette : ne repond pas / plus");
			print_at(1, 24, C_GREY, "Droite/gauche : detail par puce");
		} else {
			show_chip_detail(s, page);
			print_at(1, 25, C_GREY, "Droite/gauche : puce suivante");
		}
		print_at(1, 26, C_GREY, "Bouton 1 ou 2 : retour au menu");
		k = pad_wait();
		if (k & (B_1 | B_2 | B_TEST))
			return;
		if (k & (B_RIGHT | B_DOWN))
			page = page + 1 >= n ? -1 : page + 1;
		if (k & (B_LEFT | B_UP))
			page = page < 0 ? n - 1 : page - 1;
	}
}

/* ------------------------------------------------------------------ */
/* 3. Comparaison de deux SIMM (lecture seule)                           */

static void compare_chip_bytes(int s, int ref, int c, u32 a, u32 got, u32 exp)
{
	(void)s;
	(void)ref;
	if (got != exp)
		note_error(c, a, exp, got);
}

static void screen_compare(void)
{
	int s, rs, n;
	u32 w, i;
	s = choose_slot("Comparer : SIMM a tester", 2, 0);
	if (!s)
		return;
	rs = choose_slot("Comparer : SIMM de reference (saine)",
			 slot_is_gfx(s) ? (s == 3 ? 4 : 3) : (s == 1 ? 2 : 1), s);
	if (!rs || rs == s)
		return;
	n = slot_chips(s);
	title("Comparaison en cours");
	res_clear();
	{
		struct ident id;
		int c;
		identify(s, &id);
		for (c = 0; c < n; c++) {
			res[c].id_m = id.manuf[c];
			res[c].id_d = id.dev[c];
			res[c].id_bad = !id.ok[c];
		}
	}
	prog_last = 999;
	if (!slot_is_gfx(s)) {
		for (w = 0; w < CHIP_SIZE; w += BLK) {
			u32 *t = (u32 *)NC(dma_buf), *r = (u32 *)NC(cmp_buf);
			progress("Comparaison", w, CHIP_SIZE);
			read_block(s, 0, w, dma_buf);
			read_block(rs, 0, w, cmp_buf);
			for (i = 0; i < BLK; i++) {
				u32 x = t[i] ^ r[i];
				int c;
				if (!x)
					continue;
				for (c = 0; c < 4; c++) {
					int sh = 24 - 8 * c;
					compare_chip_bytes(s, rs, c, w + i, (t[i] >> sh) & 0xff,
							   (r[i] >> sh) & 0xff);
				}
			}
			if (abort_requested())
				break;
		}
	} else {
		int p;
		for (p = 0; p < 4; p++)
			for (w = 0; w < CHIP_SIZE / 2; w += BLK) {
				u32 *t = (u32 *)NC(dma_buf), *r = (u32 *)NC(cmp_buf);
				progress("Comparaison", p * (CHIP_SIZE / 2) + w, 2 * CHIP_SIZE);
				read_block(s, p, w, dma_buf);
				read_block(rs, p, w, cmp_buf);
				for (i = 0; i < BLK; i++) {
					u32 x = t[i] ^ r[i], a = (w + i) * 2;
					if (!x)
						continue;
					compare_chip_bytes(s, rs, 2 * p, a, t[i] >> 24, r[i] >> 24);
					compare_chip_bytes(s, rs, 2 * p, a + 1, (t[i] >> 8) & 0xff,
							   (r[i] >> 8) & 0xff);
					compare_chip_bytes(s, rs, 2 * p + 1, a, (t[i] >> 16) & 0xff,
							   (r[i] >> 16) & 0xff);
					compare_chip_bytes(s, rs, 2 * p + 1, a + 1, t[i] & 0xff,
							   r[i] & 0xff);
				}
			}
		R16(GFXBANK) = 0;
	}
	/* lignes d'adresse : l'octet faux vaut-il celui de la reference a a^(1<<k) ? */
	{
		int c, k;
		progress("Analyse des lignes d'adresse", 0, 1);
		for (c = 0; c < n; c++) {
			struct res *r = &res[c];
			u32 j, nv = r->nfirst;
			/* on reprend les premieres erreurs puis des adresses fausses
			   echantillonnees par relecture */
			for (j = 0; j < nv; j++) {
				u32 a = r->faddr[j], got = r->fgot[j];
				r->nvote++;
				for (k = 0; k < 21; k++)
					if (read_byte(rs, c, a ^ (1u << k)) == got)
						r->avote[k]++;
			}
			if (r->errors > 3) {
				u32 tries = 0;
				for (j = 0; j < 4000 && r->nvote < 64; j++) {
					u32 a = (j * 0x9e3779b1u) >> 11, got, exp;
					got = read_byte(s, c, a);
					exp = read_byte(rs, c, a);
					if (got == exp)
						continue;
					tries++;
					r->nvote++;
					for (k = 0; k < 21; k++)
						if (read_byte(rs, c, a ^ (1u << k)) == got)
							r->avote[k]++;
				}
				(void)tries;
			}
			if (r->nvote < 8)
				r->nvote = 0;   /* trop peu d'indices */
		}
		R16(GFXBANK) = 0;
	}
	verify_passes = 1;
	results_screens(s, "Comparaison");
}

/* ------------------------------------------------------------------ */
/* 4. Test ecriture / lecture (destructif)                               */

#define T_PROG  MS(5)        /* programmation d'un octet : max ~300 us */
#define T_ERASE MS(150000)   /* effacement complet : 29F016 jusqu'a ~100 s */

static int fails_in_row[8];

/* attend la fin d'une operation sur les puces 'mask' du groupe.
   rd() relit le mot, lane(v, i) extrait l'octet de la puce i,
   exp(i) valeur attendue. Retourne le masque des puces en echec. */

/* --- SIMM programme --- */

static u32 p_lane(u32 v, int c) { return (v >> (24 - 8 * c)) & 0xff; }

/* Fin d'operation par l'algorithme du bit de bascule : tant que la puce
   travaille, DQ6 change a chaque lecture. Une puce dont DQ6 ne bascule plus
   a termine (le resultat est controle a la relecture complete). DQ5 = 1
   pendant la bascule = delai interne depasse. Retourne les puces en echec. */
static u32 p_wait(u32 addr, u32 mask, u32 limit)
{
	u32 t0 = ticks(), fail = 0;
	u32 v1 = p_read(addr);
	for (;;) {
		u32 v2 = p_read(addr), c;
		for (c = 0; c < 4; c++) {
			u32 m = 1u << c;
			if (!(mask & m))
				continue;
			if (!((p_lane(v1, c) ^ p_lane(v2, c)) & 0x40)) {
				mask &= ~m;
			} else if (p_lane(v2, c) & 0x20) {
				u32 a = p_read(addr), b = p_read(addr);
				if ((p_lane(a, c) ^ p_lane(b, c)) & 0x40)
					fail |= m;
				mask &= ~m;
			}
		}
		if (!mask)
			break;
		if (ticks() - t0 > limit) {
			fail |= mask;
			break;
		}
		v1 = v2;
	}
	return fail;
}

static void p_erase(int s)
{
	u32 b = prg_base(s), fail;
	int c;
	sec_on();
	p_cmd(b, K_F0);
	p_cmd(b, K_80);
	p_cmd(b, K_10);
	fail = p_wait(b, 0xf, T_ERASE);
	p_cmd(b, K_F0);
	sec_off();
	for (c = 0; c < 4; c++)
		if (fail & (1u << c))
			res[c].erase_fail = 1;
}

/* programme toute la SIMM avec le motif (inv = complement) */
static void p_program(int s, int inv, const char *what)
{
	u32 b = prg_base(s), a;
	int c;
	sec_on();
	for (a = 0; a < CHIP_SIZE; a++) {
		u32 v = 0, mask = 0, fail;
		if ((a & 0x3fff) == 0) {
			progress(what, a, CHIP_SIZE);
			if (abort_requested())
				break;
		}
		for (c = 0; c < 4; c++) {
			u32 d = pat(c, a) ^ (inv ? 0xff : 0);
			if (res[c].dead)
				d = 0xff;
			v |= d << (24 - 8 * c);
			if (d != 0xff)
				mask |= 1u << c;
		}
		if (!mask)
			continue;
		p_cmd(b, K_A0);
		p_write(b + a * 4, v);
		fail = p_wait(b + a * 4, mask, T_PROG);
		for (c = 0; c < 4; c++) {
			if (fail & (1u << c)) {
				res[c].timeouts++;
				if (++fails_in_row[c] > 64)
					res[c].dead = 1;
			} else if (mask & (1u << c)) {
				fails_in_row[c] = 0;
			}
		}
		p_cmd(b, K_F0);    /* comme le BIOS d'origine apres chaque mot */
	}
	sec_off();
}

static void p_verify(int s, int mode, const char *what)
{
	/* mode 0 : tout a FF ; 1 : motif ; 2 : motif inverse */
	u32 w, i;
	int c;
	for (w = 0; w < CHIP_SIZE; w += BLK) {
		u32 *d = (u32 *)NC(dma_buf);
		progress(what, w, CHIP_SIZE);
		read_block(s, 0, w, dma_buf);
		for (i = 0; i < BLK; i++) {
			u32 a = w + i;
			for (c = 0; c < 4; c++) {
				u32 got = p_lane(d[i], c), exp;
				exp = mode == 0 ? 0xff : (pat(c, a) ^ (mode == 2 ? 0xff : 0));
				if (got != exp) {
					note_error(c, a, exp, got);
					if (mode)
						vote_alias(c, a, got, mode == 2);
				}
			}
		}
	}
}

/* --- SIMM graphique --- */

static u32 g_lane(u32 v, int lane) { return (v >> (24 - 8 * lane)) & 0xff; }

/* lanes du mot : 0 = puce paire octet pair, 1 = puce impaire octet pair,
   2 = puce paire octet impair, 3 = puce impaire octet impair */

/* comme le BIOS d'origine, lecture 16 bits : un seul acces par puce
   (une lecture 32 bits lit deux fois chaque puce et DQ6 semblerait fixe).
   octet haut = puce paire, octet bas = puce impaire ; mask bit0/bit1. */
static u32 g_wait(u32 off, u32 odd, u32 mask, u32 limit)
{
	u32 t0 = ticks(), fail = 0;
	u32 addr = GFXWIN + off * 4 + (odd ? 2 : 0);
	u32 v1 = R16(addr);
	for (;;) {
		u32 v2 = R16(addr), l;
		for (l = 0; l < 2; l++) {
			u32 m = 1u << l;
			u32 x1 = l ? v1 & 0xff : v1 >> 8, x2 = l ? v2 & 0xff : v2 >> 8;
			if (!(mask & m))
				continue;
			if (!((x1 ^ x2) & 0x40)) {
				mask &= ~m;
			} else if (x2 & 0x20) {
				u32 a = R16(addr), b = R16(addr);
				u32 y1 = l ? a & 0xff : a >> 8, y2 = l ? b & 0xff : b >> 8;
				if ((y1 ^ y2) & 0x40)
					fail |= m;
				mask &= ~m;
			}
		}
		if (!mask)
			break;
		if (ticks() - t0 > limit) {
			fail |= mask;
			break;
		}
		v1 = v2;
	}
	return fail;
}

static void g_erase(int s)
{
	int p;
	sec_on();
	/* lance l'effacement des 4 paires, puis attend chacune */
	for (p = 0; p < 4; p++) {
		g_cmd_we(s, p, 0xf0f0);
		gfx_bank(s, p, 0);
		we_on();
		g_cmd(0x8080);
		R16(GFXWIN + 0x2aa * 4 + 2) = 0xaaaa;
		R16(GFXWIN + 0x155 * 4 + 0) = 0x5555;
		R16(GFXWIN + 0x2aa * 4 + 2) = 0x1010;
		we_off();
	}
	for (p = 0; p < 4; p++) {
		u32 fail;
		gfx_bank(s, p, 0);
		fail = g_wait(0, 0, 0x3, T_ERASE);
		if (fail & 1)
			res[2 * p].erase_fail = 1;
		if (fail & 2)
			res[2 * p + 1].erase_fail = 1;
		g_cmd_we(s, p, 0xf0f0);
	}
	R16(GFXBANK) = 0;
	sec_off();
}

static void g_program(int s, int inv, const char *what)
{
	int p;
	u32 a;
	sec_on();
	for (p = 0; p < 4; p++) {
		int c0 = 2 * p, c1 = 2 * p + 1;
		for (a = 0; a < CHIP_SIZE; a++) {
			/* a = adresse dans chaque puce de la paire */
			u32 w = a >> 1, half = w >= 0x80000, off = w & 0x7ffff;
			u32 odd = a & 1;
			u32 d0 = pat(c0, a) ^ (inv ? 0xff : 0);
			u32 d1 = pat(c1, a) ^ (inv ? 0xff : 0);
			u32 lm, fail;
			if ((a & 0x3fff) == 0) {
				progress(what, p * CHIP_SIZE + a, 4 * CHIP_SIZE);
				if (abort_requested())
					goto out;
			}
			if (res[c0].dead)
				d0 = 0xff;
			if (res[c1].dead)
				d1 = 0xff;
			if (d0 == 0xff && d1 == 0xff)
				continue;
			gfx_bank(s, p, 0);
			we_on();
			g_cmd(0xa0a0);
			gfx_bank(s, p, half);
			R16(GFXWIN + off * 4 + (odd ? 2 : 0)) = (d0 << 8) | d1;
			we_off();
			lm = ((d0 != 0xff) ? 1 : 0) | ((d1 != 0xff) ? 2 : 0);
			fail = g_wait(off, odd, lm, T_PROG);
			if (fail) {
				if (fail & 1) {
					res[c0].timeouts++;
					if (++fails_in_row[c0] > 64)
						res[c0].dead = 1;
				}
				if (fail & 2) {
					res[c1].timeouts++;
					if (++fails_in_row[c1] > 64)
						res[c1].dead = 1;
				}
			} else {
				if (lm & 1)
					fails_in_row[c0] = 0;
				if (lm & 2)
					fails_in_row[c1] = 0;
			}
			/* comme le BIOS d'origine apres chaque ecriture */
			g_cmd_we(s, p, 0xf0f0);
		}
	}
out:
	R16(GFXBANK) = 0;
	sec_off();
}

static void g_verify(int s, int mode, const char *what)
{
	int p;
	u32 w, i;
	for (p = 0; p < 4; p++) {
		int c0 = 2 * p, c1 = 2 * p + 1;
		for (w = 0; w < CHIP_SIZE / 2; w += BLK) {
			u32 *d = (u32 *)NC(dma_buf);
			progress(what, p * (CHIP_SIZE / 2) + w, 2 * CHIP_SIZE);
			read_block(s, p, w, dma_buf);
			for (i = 0; i < BLK; i++) {
				u32 a = (w + i) * 2, v = d[i], l;
				for (l = 0; l < 4; l++) {
					int c = (l & 1) ? c1 : c0;
					u32 aa = a + (l >> 1);
					u32 got = g_lane(v, l), exp;
					exp = mode == 0 ? 0xff : (pat(c, aa) ^ (mode == 2 ? 0xff : 0));
					if (got != exp) {
						note_error(c, aa, exp, got);
						if (mode)
							vote_alias(c, aa, got, mode == 2);
					}
				}
			}
		}
	}
	R16(GFXBANK) = 0;
}

static int confirm_erase(int s)
{
	title("Test ecriture/lecture");
	print_at(1, 5, C_RED, "ATTENTION : la SIMM ");
	putdec(s);
	puts_(" va etre EFFACEE.");
	print_at(1, 7, C_WHITE, "Le test efface, ecrit deux motifs (le motif\n"
		 "puis son inverse) et relit chaque octet.\n"
		 "La SIMM reste VIERGE a la fin : il faudra la\n"
		 "reprogrammer.");
	print_at(1, 13, C_GREY, "Duree : ~1-2 min (programme), ~5-10 min (graph.)");
	print_at(1, 14, C_GREY, "Abandon pendant le test : boutons 2+3 ensemble.");
	print_at(1, 17, C_YELLOW, "Bouton 3 : CONFIRMER     Bouton 2 : annuler");
	for (;;) {
		u32 k = pad_wait();
		if (k & B_3)
			return 1;
		if (k & (B_2 | B_1))
			return 0;
	}
}

static void screen_write(void)
{
	int s = choose_slot("Test ecriture/lecture (EFFACE la SIMM)", 3, 0);
	struct ident id;
	int c;
	if (!s || !confirm_erase(s))
		return;
	title("Test ecriture/lecture en cours");
	res_clear();
	memset(fails_in_row, 0, sizeof(fails_in_row));
	identify(s, &id);
	for (c = 0; c < slot_chips(s); c++) {
		res[c].id_m = id.manuf[c];
		res[c].id_d = id.dev[c];
		res[c].id_bad = !id.ok[c];
	}
	prog_last = 999;
	print_at(2, 18, C_WHITE, "1/6 effacement");
	if (slot_is_gfx(s)) g_erase(s); else p_erase(s);
	print_at(2, 18, C_WHITE, "2/6 controle effacement");
	if (slot_is_gfx(s)) g_verify(s, 0, "Lecture"); else p_verify(s, 0, "Lecture");
	print_at(2, 18, C_WHITE, "3/6 ecriture motif     ");
	if (slot_is_gfx(s)) g_program(s, 0, "Ecriture"); else p_program(s, 0, "Ecriture");
	print_at(2, 18, C_WHITE, "4/6 relecture motif    ");
	if (slot_is_gfx(s)) g_verify(s, 1, "Lecture"); else p_verify(s, 1, "Lecture");
	print_at(2, 18, C_WHITE, "5/6 effacement + inverse");
	if (slot_is_gfx(s)) { g_erase(s); g_program(s, 1, "Ecriture"); }
	else { p_erase(s); p_program(s, 1, "Ecriture"); }
	print_at(2, 18, C_WHITE, "6/6 relecture inverse   ");
	if (slot_is_gfx(s)) g_verify(s, 2, "Lecture"); else p_verify(s, 2, "Lecture");
	print_at(2, 18, C_WHITE, "effacement final        ");
	if (slot_is_gfx(s)) g_erase(s); else p_erase(s);
	for (c = 0; c < 8; c++)
		if (res[c].nvote < 8)
			res[c].nvote = 0;
	verify_passes = 3;
	results_screens(s, "Test ecriture");
}

/* ------------------------------------------------------------------ */

void exception(void)
{
	cls();
	print_at(1, 1, C_RED, "EXCEPTION DU PROCESSEUR - redemarrer");
	for (;;)
		;
}

int main(void)
{
	static const char *const items[4] = {
		"1. Identification des puces (rapide)",
		"2. Controle du contenu (CRC par puce)",
		"3. Comparer a une SIMM saine (lecture)",
		"4. Test ecriture/lecture (EFFACE !)",
	};
	int sel = 0, i;
	timer_init();
#ifdef OWN_BOOT
	hw_init();
#endif
	video_init();
	crc_init();
	for (;;) {
		u32 k;
		title("Menu");
		for (i = 0; i < 4; i++) {
			print_at(2, 6 + 2 * i, i == sel ? C_YELLOW : C_WHITE,
				 i == sel ? "> " : "  ");
			puts_(items[i]);
		}
		print_at(1, 17, C_GREY, "Haut/bas : choisir   Bouton 1 : valider");
		print_at(1, 19, C_GREY, "Les numeros de puce .0 a .7 suivent les");
		print_at(1, 20, C_GREY, "fichiers MAME (ex. sfiii3-simm3.5).");
		pal_fixes += clear_check(PALRAM, 2 * 0x17e00, 0);
		print_at(1, 25, C_GREY, "v8 - " __DATE__);
		k = pad_wait();
		if (k & B_UP)
			sel = (sel + 3) & 3;
		if (k & B_DOWN)
			sel = (sel + 1) & 3;
		if (k & (B_1 | B_TEST)) {
			switch (sel) {
			case 0: screen_ident(); break;
			case 1: screen_crc(); break;
			case 2: screen_compare(); break;
			case 3: screen_write(); break;
			}
		}
	}
	return 0;
}
