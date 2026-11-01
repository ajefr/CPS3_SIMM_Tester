#!/usr/bin/env python3
"""
Construit le BIOS de test des SIMM CPS3.

  1. genere font.h (police 8x8 domaine public) et refs.h (CRC32 de chaque puce
     des sets MAME, lus dans cps3.cpp, + vos propres dumps avec --dumps) ;
  2. compile le programme SH-2 (sh-elf-gcc) ;
  3. assemble l'image : boot.S (demarrage, sans code Capcom) en 0x0000,
     programme en 0x2000, constantes lues par DMA en 0x1FF00-0x1FF3F ;
  4. chiffre l'ensemble pour la cle de la cartouche cible.

Exemples :
  python3 construire_bios.py --key dead --out simmtest_cartouche_morte.bin
  python3 construire_bios.py --key sfiii3 --out simmtest_sfiii3.bin
  python3 construire_bios.py --key none --out simmtest_sh2_standard.bin   (SH-2 standard)
  ajouter --lang en pour les textes en anglais.
  Ancien mode (v1-v5) : --base <BIOS d'origine> garde le boot Capcom (0x0000-0x1FFF).
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

KEYS = {   # driver MAME src/mame/capcom/cps3.cpp
    "dead":     (0x00000000, 0x00000000),   # cartouche morte = sfiii2
    "sfiii2":   (0x00000000, 0x00000000),
    "redearth": (0x9e300ab1, 0xa175b82c),
    "warzard":  (0x9e300ab1, 0xa175b82c),
    "sfiii":    (0xb5fe053e, 0xfc03925a),
    "jojo":     (0x02203ee3, 0x01301972),
    "sfiii3":   (0xa55432b4, 0x0c129981),
    "jojoba":   (0x23323ee3, 0x03021972),
}

BIOS_SIZE = 0x80000
CACHE_CODE = (0x510, 0x544)     # copie en 0xC0000000 au boot
CACHE_INSN_END = 0x532          # instructions jusqu'a 0x531 ; ensuite pool de constantes
                                # (les lectures de donnees dans le cache ne sont pas dechiffrees)
RAW_KEEP = (0x1FF00, 0x1FF40)   # lu par DMA : octets bruts
ENTRY_LITERAL = 0x4C8           # adresse du programme principal (rts du boot)
EXC_VECTORS = (4, 6, 9, 10)     # instr. illegale, slot illegal, erreurs d'adresse
CODE_BASE = 0x2000


def rotl16(v, n):
    return ((v << n) | (v >> (16 - n))) & 0xFFFF


def rotxor(val, x):
    res = (val + rotl16(val, 2)) & 0xFFFF
    return rotl16(res, 4) ^ (res & (val ^ x))


def mask(address, key1, key2):
    address ^= key1
    val = (address & 0xFFFF) ^ 0xFFFF
    val = rotxor(val, key2 & 0xFFFF)
    val ^= (address >> 16) ^ 0xFFFF
    val = rotxor(val, key2 >> 16)
    val ^= (address & 0xFFFF) ^ (key2 & 0xFFFF)
    return (val | (val << 16)) & 0xFFFFFFFF


def words(b):
    return list(struct.unpack(">%dI" % (len(b) // 4), b))


def key_of(name):
    if name in ("none", "sh2std"):
        return None     # SH-2 standard : aucun chiffrement
    if name in KEYS:
        return KEYS[name]
    k1, k2 = name.split(",")
    return int(k1, 16), int(k2, 16)


# ---------------------------------------------------------------- generation

def gen_font(path_basic, path_latin, out):
    def parse(p):
        rows = re.findall(r"\{\s*((?:0x[0-9A-Fa-f]{2}\s*,\s*){7}0x[0-9A-Fa-f]{2})\s*\}", open(p).read())
        return [[int(x, 16) for x in r.split(",")] for r in rows]
    basic = parse(path_basic)
    latin = parse(path_latin)
    assert len(basic) == 128 and len(latin) == 96, (len(basic), len(latin))
    font = basic + [[0] * 8] * 32 + latin
    with open(out, "w") as f:
        f.write("/* genere par construire_bios.py - police font8x8 (domaine public) */\n")
        f.write("static const u8 font8x8[256][8] = {\n")
        for g in font:
            f.write("\t{" + ",".join("0x%02x" % x for x in g) + "},\n")
        f.write("};\n")


def gen_refs(cps3_cpp, dumps, out):
    refs = {}
    cur = None
    if not os.path.exists(cps3_cpp):
        # pas de source MAME : on repart de la liste livree (refs_mame.h)
        for crc, name in re.findall(r'\{ 0x([0-9a-f]{8}), "([^"]+)" \}', open(os.path.join(HERE, "refs_mame.h")).read()):
            refs.setdefault(int(crc, 16), name)
    for line in (open(cps3_cpp) if os.path.exists(cps3_cpp) else []):
        m = re.match(r"ROM_START\( *(\w+) *\)", line)
        if m:
            cur = m.group(1)
        m = re.search(r'ROM_LOAD\( "[a-z0-9]+-(simm\d\.\d)", 0x00000, 0x200000, CRC\(([0-9a-f]{8})\)', line)
        if m and cur:
            # nom du set MAME (revision) + position de la puce
            refs.setdefault(int(m.group(2), 16), "%s %s" % (cur, m.group(1)))
    for d in dumps or []:
        for root, _, files in os.walk(d):
            for fn in sorted(files):
                p = os.path.join(root, fn)
                if os.path.getsize(p) != 0x200000:
                    continue
                crc = zlib.crc32(open(p, "rb").read()) & 0xFFFFFFFF
                refs.setdefault(crc, os.path.splitext(fn)[0][:23] if "simm" not in fn else fn[:23])
    blank = zlib.crc32(b"\xff" * 0x200000) & 0xFFFFFFFF
    refs.pop(blank, None)
    with open(out, "w") as f:
        f.write("/* genere par construire_bios.py */\n")
        f.write("static const struct ref refs[] = {\n")
        for crc, name in sorted(refs.items(), key=lambda kv: kv[1]):
            f.write('\t{ 0x%08x, "%s" },\n' % (crc, name))
        f.write("};\n#define NREFS %d\n" % len(refs))
    return len(refs)


# ---------------------------------------------------------------- compilation

def run(cmd):
    r = subprocess.run(cmd, cwd=HERE, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(" ".join(cmd) + "\n" + r.stdout + r.stderr)
        sys.exit(1)
    return r.stdout


def compile_prog(gcc, prefix, lang, own):
    cflags = ["-m2", "-mb", "-Os", "-ffreestanding", "-fno-builtin", "-nostdlib",
              "-fno-common", "-Wall", "-Wno-unused-function"]
    if own:
        cflags.append("-DOWN_BOOT")
    run([gcc] + cflags + ["-c", "crt0.S", "-o", "crt0.o"])
    objs = ["crt0.o", "simmtest.o"]
    if own:
        run([gcc] + cflags + ["-c", "boot.S", "-o", "boot.o"])
        objs = ["boot.o"] + objs
    src = "simmtest.c"
    if lang == "en":
        run([sys.executable, os.path.join(HERE, "traduire_en.py")])
        src = "simmtest_en.c"
    run([gcc] + cflags + ["-c", src, "-o", "simmtest.o"])
    libgcc = run([gcc, "-m2", "-mb", "-print-libgcc-file-name"]).strip()
    link = [prefix + "ld", "-EB", "-T", "link.ld" if own else "link_capcom.ld",
            "-Map", "simmtest.map", "-o", "simmtest.elf"] + objs
    if os.path.exists(libgcc):
        link.append(libgcc)
    run(link)
    syms = {}
    for line in run([prefix + "nm", "simmtest.elf"]).splitlines():
        a, _, n = line.split()
        syms[n] = int(a, 16)
    hdr = run([prefix + "objdump", "-h", "simmtest.elf"])
    for line in hdr.splitlines():
        p = line.split()
        if len(p) > 3 and p[1] == ".data" and int(p[2], 16):
            sys.exit("erreur : section .data non vide (variables initialisees interdites)")
    if own:
        run([prefix + "objcopy", "-O", "binary", "--gap-fill", "0xff", "--pad-to", hex(BIOS_SIZE),
             "simmtest.elf", "image.bin"])
    run([prefix + "objcopy", "-O", "binary", "-j", ".text", "simmtest.elf", "text.bin"])
    run([prefix + "objcopy", "-O", "binary", "-j", ".rodata", "simmtest.elf", "rodata.bin"])
    text = open(os.path.join(HERE, "text.bin"), "rb").read()
    rodata = open(os.path.join(HERE, "rodata.bin"), "rb").read()
    if CODE_BASE + len(text) > RAW_KEEP[0]:
        sys.exit("erreur : code trop gros (%d octets)" % len(text))
    if 0x20000 + len(rodata) > BIOS_SIZE:
        sys.exit("erreur : donnees trop grosses")
    return text, rodata, syms


# ---------------------------------------------------------------- BIOS

def build_image(base_raw, base_key, text, rodata, syms, out_key):
    raw = words(base_raw)
    k1, k2 = base_key
    plain = [raw[i] ^ mask(i * 4, k1, k2) for i in range(len(raw))]
    # instructions reelles du code copie en cache
    cache_instr = {}
    for a in range(CACHE_CODE[0], CACHE_CODE[1], 4):
        cache_instr[a] = plain[a // 4] ^ mask(0xC0000000 + a - CACHE_CODE[0], k1, k2)

    img = list(plain)
    # programme : .text en 0x2000, .rodata en 0x20000
    blob = text + b"\0" * (-len(text) % 4)
    for i, w in enumerate(words(blob)):
        img[CODE_BASE // 4 + i] = w
    for i in range(CODE_BASE // 4 + len(blob) // 4, RAW_KEEP[0] // 4):
        img[i] = 0xFFFFFFFF
    blob = rodata + b"\0" * (-len(rodata) % 4)
    for i in range(0x20000 // 4, BIOS_SIZE // 4):
        img[i] = 0xFFFFFFFF
    for i, w in enumerate(words(blob)):
        img[0x20000 // 4 + i] = w
    assert syms["_start"] == CODE_BASE
    img[ENTRY_LITERAL // 4] = syms["_start"]
    for v in EXC_VECTORS:
        img[v] = syms["_exc_entry"]

    out = []
    if out_key is None:
        # SH-2 standard (HD6417095) : le processeur lit tout en clair ; le code
        # copie en cache est execute tel quel, le pool de constantes aussi.
        for i, w in enumerate(img):
            a = i * 4
            if RAW_KEEP[0] <= a < RAW_KEEP[1]:
                out.append(raw[i])
            elif CACHE_CODE[0] <= a < CACHE_CODE[1]:
                ins, dat = cache_instr[a], w
                hi = ins if a < CACHE_INSN_END else dat
                lo = ins if a + 2 < CACHE_INSN_END else dat
                out.append((hi & 0xFFFF0000) | (lo & 0xFFFF))
            else:
                out.append(w)
        return struct.pack(">%dI" % len(out), *out)
    o1, o2 = out_key
    for i, w in enumerate(img):
        a = i * 4
        if RAW_KEEP[0] <= a < RAW_KEEP[1]:
            out.append(raw[i])
        elif CACHE_CODE[0] <= a < CACHE_CODE[1]:
            # par demi-mot : instruction (double masque) ou constante (masque normal)
            ins = cache_instr[a] ^ mask(0xC0000000 + a - CACHE_CODE[0], o1, o2) ^ mask(a, o1, o2)
            dat = w ^ mask(a, o1, o2)
            hi = ins if a < CACHE_INSN_END else dat
            lo = ins if a + 2 < CACHE_INSN_END else dat
            out.append((hi & 0xFFFF0000) | (lo & 0xFFFF))
        else:
            out.append(w ^ mask(a, o1, o2))
    return struct.pack(">%dI" % len(out), *out)


OWN_CACHE = (0x510, 0x550)      # routine de boot.S copiee dans le cache : instructions seules


def build_own_image(out_key):
    """Image sans code d'origine (boot.S) : chiffre tout sauf les constantes
    lues par DMA ; la routine copiee dans le cache prend le double masque
    (lue comme donnee a son adresse, puis executee depuis 0xC0000000)."""
    img = words(open(os.path.join(HERE, "image.bin"), "rb").read())
    assert len(img) == BIOS_SIZE // 4
    if out_key is None:
        return struct.pack(">%dI" % len(img), *img)
    o1, o2 = out_key
    out = []
    for i, w in enumerate(img):
        a = i * 4
        if RAW_KEEP[0] <= a < RAW_KEEP[1]:
            out.append(w)
        elif OWN_CACHE[0] <= a < OWN_CACHE[1]:
            out.append(w ^ mask(a, o1, o2) ^ mask(0xC0000000 + a - OWN_CACHE[0], o1, o2))
        else:
            out.append(w ^ mask(a, o1, o2))
    return struct.pack(">%dI" % len(out), *out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", help="ancien mode : BIOS de cartouche d'origine (29F400, 512 Ko) dont on garde le boot ; "
                    "sans cette option, le BIOS est entierement genere (boot.S), sans code d'origine")
    ap.add_argument("--base-key", default="dead", help="cle du BIOS de base (dead, sfiii3, ... ou k1,k2 en hexa)")
    ap.add_argument("--key", action="append", required=True,
                    help="cle de la cartouche qui recevra le BIOS (repetable) ; none = SH-2 standard non chiffre")
    ap.add_argument("--out", action="append", required=True, help="fichier de sortie (un par --key)")
    ap.add_argument("--cps3-cpp", default=os.path.join(HERE, "cps3.cpp"), help="driver MAME pour les CRC")
    ap.add_argument("--dumps", action="append", help="dossier de dumps de puces (fichiers de 2 Mo) a reconnaitre")
    ap.add_argument("--lang", choices=["fr", "en"], default="fr", help="langue des textes affiches")
    ap.add_argument("--gcc", default="sh-elf-gcc")
    ap.add_argument("--binutils-prefix", default="sh-elf-")
    args = ap.parse_args()
    if len(args.key) != len(args.out):
        sys.exit("autant de --out que de --key")

    gen_font(os.path.join(HERE, "font8x8_basic.h"), os.path.join(HERE, "font8x8_ext_latin.h"),
             os.path.join(HERE, "font.h"))
    n = gen_refs(args.cps3_cpp, args.dumps, os.path.join(HERE, "refs.h"))
    print("references : %d puces" % n)
    own = not args.base
    text, rodata, syms = compile_prog(args.gcc, args.binutils_prefix, args.lang, own)
    print("code : %d octets, donnees : %d octets" % (len(text), len(rodata)))

    if not own:
        base = open(args.base, "rb").read()
        if len(base) != BIOS_SIZE:
            sys.exit("le BIOS de base doit faire 512 Ko")
    for k, o in zip(args.key, args.out):
        if own:
            img = build_own_image(key_of(k))
        else:
            img = build_image(base, key_of(args.base_key), text, rodata, syms, key_of(k))
        open(o, "wb").write(img)
        print("%s : cle %s, CRC32 %08x" % (o, k, zlib.crc32(img) & 0xFFFFFFFF))


if __name__ == "__main__":
    main()
