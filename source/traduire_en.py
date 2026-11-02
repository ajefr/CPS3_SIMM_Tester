#!/usr/bin/env python3
"""Ecrit simmtest_en.c : simmtest.c avec les textes affiches en anglais."""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
T = [
    ("CPS3  TEST DES SIMM FLASH", "CPS3  FLASH SIMM TESTER"),
    ("Bouton 1 ou 2 : retour au menu", "Button 1 or 2: back to menu"),
    ("Bouton 1 ou 2 : retour", "Button 1 or 2: back"),
    ("Identification des puces (commande 90h)", "Chip identification (command 90h)"),
    ("fabricant/composant, 29F016 = xx/AD", "maker/device, 29F016 = xx/AD"),
    (" graph. ", " gfx    "),
    (" progr. ", " prog.  "),
    ("absente ou aucune puce ne repond", "absent or no chip answers"),
    (" repondent (", " answer ("),
    ("SIMM 32 Mbit : seules .0 et .1 sont montees.", "32 Mbit SIMM: only .0 and .1 are fitted."),
    ("Gauche/droite : slot   Bouton 1 : valider", "Left/right: slot   Button 1: select"),
    ("Bouton 2 : annuler", "Button 2: cancel"),
    ("  (graphique, 8 puces)  >", "  (graphics, 8 chips)  >"),
    ("  (programme, 4 puces)  >", "  (program, 4 chips)  >"),
    ("Analyse des lignes d'adresse", "Checking address lines"),
    ("Lecture", "Reading"),
    ("Ecriture", "Writing"),
    ("Controle du contenu (CRC32 par puce)", "Content check (CRC32 per chip)"),
    ("Controle du contenu - lecture en cours", "Content check - reading"),
    ("Controle du contenu  SIMM ", "Content check  SIMM "),
    ("Controle du contenu", "Content check"),
    ("vierge (tout a FF)", "blank (all FF)"),
    ("INCONNU", "UNKNOWN"),
    (" attendu ", " expected "),
    ("ID lu ", "ID read "),
    ("toujours 0 : ", "always 0: "),
    ("toujours 1 : ", "always 1: "),
    ("Toutes les puces correspondent a un dump MAME.", "All chips match a MAME dump."),
    ("Puce(s) INCONNU(E)S : contenu different du dump.", "UNKNOWN chip(s): content differs from dump."),
    ("Faire le test 3 (comparaison) ou 4 (ecriture).", "Run test 3 (compare) or 4 (write)."),
    ("Aucun dump reconnu : comparer a une SIMM saine.", "No dump recognised: compare to a good SIMM."),
    ("Detail ", "Detail "),
    ("  (bus ", "  (bus "),
    ("  (attendu xx/AD : ID faux)", "  (expected xx/AD: bad ID)"),
    ("La puce ne repond plus (delai depasse).", "Chip stopped answering (timeout)."),
    ("Effacement en echec (DQ5 / delai)", "Erase failed (DQ5 / timeout)"),
    (" ou D5 a 1", " or D5 stuck 1"),
    ("Octets faux : ", "Bad bytes: "),
    (" relectures)", " rereads)"),
    ("adr puce ", "chip adr "),
    ("  attendu ", "  expected "),
    ("  lu ", "  read "),
    ("bit  lu 0 (attendu 1)  lu 1 (attendu 0)", "bit  read 0 (exp. 1)   read 1 (exp. 0)"),
    ("lignes d'adresse suspectes : ", "suspect address lines: "),
    ("  mais ID lu ", "  but ID read "),
    ("HS muette ", "BAD dead "),
    ("HS ", "BAD "),
    (" puce(s) en defaut.", " bad chip(s)."),
    ("Aucune erreur.", "No errors."),
    ("D7..0 : un caractere par bit de donnee", "D7..0: one character per data bit"),
    (" .  bon     0  lu 0 au lieu de 1", " .  good    0  read 0 instead of 1"),
    (" 1  lu 1 au lieu de 0    x  les deux", " 1  read 1 instead of 0    x  both"),
    ("Ax : ligne d'adresse suspecte", "Ax: suspect address line"),
    ("muette : ne repond pas / plus", "dead: does not answer (any more)"),
    ("Droite/gauche : detail par puce", "Right/left: per-chip detail"),
    ("Droite/gauche : puce suivante", "Right/left: next chip"),
    ("Comparer : SIMM a tester", "Compare: SIMM to test"),
    ("Comparer : SIMM de reference (saine)", "Compare: reference SIMM (good)"),
    ("Comparaison en cours", "Comparing"),
    ("Comparaison", "Compare"),
    ("Test ecriture/lecture (EFFACE la SIMM)", "Write/read test (ERASES the SIMM)"),
    ("Test ecriture/lecture en cours", "Write/read test running"),
    ("Test ecriture/lecture", "Write/read test"),
    ("Test ecriture", "Write test"),
    ("ATTENTION : la SIMM ", "WARNING: SIMM "),
    (" va etre EFFACEE.", " will be ERASED."),
    ("Le test efface, ecrit deux motifs (le motif\\n", "The test erases, writes two patterns (the\\n"),
    ("puis son inverse) et relit chaque octet.\\n", "pattern then its inverse), reads every byte.\\n"),
    ("La SIMM reste VIERGE a la fin : il faudra la\\n", "The SIMM is left BLANK: you will have to\\n"),
    ("reprogrammer.", "reprogram it."),
    ("Duree : ~1-2 min (programme), ~5-10 min (graph.)", "Time: ~1-2 min (program), ~5-10 min (gfx)"),
    ("Abandon pendant le test : boutons 2+3 ensemble.", "Abort during test: buttons 2+3 together."),
    ("Bouton 3 : CONFIRMER     Bouton 2 : annuler", "Button 3: CONFIRM     Button 2: cancel"),
    ("1/6 effacement", "1/6 erase"),
    ("2/6 controle effacement", "2/6 blank check"),
    ("3/6 ecriture motif     ", "3/6 write pattern      "),
    ("4/6 relecture motif    ", "4/6 verify pattern     "),
    ("5/6 effacement + inverse", "5/6 erase + inverse    "),
    ("6/6 relecture inverse   ", "6/6 verify inverse      "),
    ("effacement final        ", "final erase             "),
    ("EXCEPTION DU PROCESSEUR - redemarrer", "CPU EXCEPTION - restart"),
    ("1. Identification des puces (rapide)", "1. Chip identification (fast)"),
    ("2. Controle du contenu (CRC par puce)", "2. Content check (CRC per chip)"),
    ("3. Comparer a une SIMM saine (lecture)", "3. Compare to a good SIMM (read)"),
    ("4. Test ecriture/lecture (EFFACE !)", "4. Write/read test (ERASES!)"),
    ("Haut/bas : choisir   Bouton 1 : valider", "Up/down: choose   Button 1: select"),
    ("Les numeros de puce .0 a .7 suivent les", "Chip numbers .0 to .7 follow the"),
    ("fichiers MAME (ex. sfiii3-simm3.5).", "MAME file names (e.g. sfiii3-simm3.5)."),
    ("Menu", "Menu"),
    ("Non detectee : aucune puce ne repond", "Not detected: no chip answers"),
    ("Detectee : ", "Detected: "),
    (" puces repondent (", " chips answer ("),
    ("32 Mbit, ", "32 Mbit, "),
    ("29F016 : xx/AD   28F016 : 89/A0 ou B0/88", "29F016: xx/AD   28F016: 89/A0 or B0/88"),
    ("  (attendu xx/AD ou 89/A0 : ID faux)", "  (expected xx/AD or 89/A0: bad ID)"),
    ("  (type 28F016)", "  (28F016 type)"),
    ("Sharp", "Sharp"),
    ("Erreurs relevees jusqu'ici :", "Errors found so far:"),
    ("Hitachi", "Hitachi"),
    ("/2 repondent (", "/2 answer ("),
    ("Detectee : 32 Mbit, ", "Detected: 32 Mbit, "),
    ("SIMM 32 Mbit : .2 a .7 absentes, non testees.", "32 Mbit SIMM: .2 to .7 not fitted, not tested."),
]

def main():
    src = open(os.path.join(HERE, "simmtest.c"), encoding="latin-1").read()
    d = dict(T)
    def rep(m):
        s = m.group(1)
        return '"' + d.get(s, s) + '"'
    out = re.sub(r'"((?:[^"\\\n]|\\.)*)"', rep, src)
    # textes a traduire restes en francais ?
    left = set()
    for line in out.splitlines():
        if line.lstrip().startswith(("/*", "*", "#include")):
            continue
        for s in re.findall(r'"((?:[^"\\\n]|\\.)*)"', line.split("/*")[0]):
            if re.search(r"[a-z]{3}", s) and s not in d.values():
                left.add(s)
    for s in sorted(left):
        print("non traduit :", repr(s), file=sys.stderr)
    for k, v in T:
        if len(v.replace("\\n", "")) > 47:
            print("trop long :", v, file=sys.stderr)
    open(os.path.join(HERE, "simmtest_en.c"), "w", encoding="latin-1").write(out)

if __name__ == "__main__":
    main()
