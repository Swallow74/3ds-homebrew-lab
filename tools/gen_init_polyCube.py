#!/usr/bin/env python3
"""
gen_init_polyCube.py  -  genera source/InitPolyCube.cpp a partire da
dl/BL_SRC/BlockOut/InitPolyCube.cpp (BlockOut II 2.5, GPL).

L'originale costruiva le display-list OpenGL con Create(cubeSide,origin,ghost,
wEdge); nel port 3DS la geometria e' solo memorizzata (SetGeometry) e il
renderer disegna i cubi dalla lista. I dati (AddCube/SetInfo) sono copiati
esattamente: questo script garantisce che non ci siano errori di trascrizione.
"""

import re
import sys
import os

SRC = "dl/BL_SRC/BlockOut/InitPolyCube.cpp"
DST = "project/blockout-3ds/source/InitPolyCube.cpp"

txt = open(SRC, encoding="latin-1").read()

# scarta le parti protette da #ifdef AI_TEST / #ifndef AI_TEST
body = txt.split("#include \"BlockOrientation.h\"", 1)[1]

pieces = {}   # idx -> dict(cubes=[], info=None)
cur = None
for line in body.splitlines():
    s = line.strip()
    if not s:
        continue
    m = re.match(r"^allPolyCube\[(\d+)\]\.AddCube\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)", s)
    if m:
        idx = int(m.group(1))
        pieces.setdefault(idx, {"cubes": [], "info": None})
        pieces[idx]["cubes"].append((int(m.group(2)), int(m.group(3)), int(m.group(4))))
        continue
    m = re.match(r"^allPolyCube\[(\d+)\]\.SetInfo\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(TRUE|FALSE)\s*,\s*(TRUE|FALSE)\s*\)", s)
    if m:
        idx = int(m.group(1))
        pieces.setdefault(idx, {"cubes": [], "info": None})
        pieces[idx]["info"] = (int(m.group(2)), int(m.group(3)),
                               m.group(4) == "TRUE", m.group(5) == "TRUE")
        continue

# le orientazioni stanno in BlockOrientation.h (incluso tale e quale)
orient = {}
bo = open("dl/BL_SRC/BlockOut/BlockOrientation.h", encoding="latin-1").read()
for m in re.finditer(r"allPolyCube\[(\d+)\]\.AddOrientation\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)", bo):
    orient.setdefault(int(m.group(1)), []).append(
        (int(m.group(2)), int(m.group(3)), int(m.group(4))))

assert len(pieces) == 41, "pezzi trovati: %d" % len(pieces)
assert len(orient) == 41, "orientazioni: %d" % len(orient)
for i in range(41):
    assert i in pieces and pieces[i]["info"], "piece %d mancante" % i

out = []
out.append("""/*
  File:        InitPolyCube.cpp
  Description: Initialise i 41 polycubi del gioco (generato da
               tools/gen_init_polyCube.py a partire da InitPolyCube.cpp di
               BlockOut II 2.5 - GPL)
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#include "InitPolyCube.h"

void InitPolyCubes(PolyCube *allPolyCube, float cubeSide, VERTEX origin, int transparent)
{

  VERTEX org = origin;

  // Initialise polycube orientations (needed by the AI player)
#include "BlockOrientation.h"
""")

for i in range(41):
    p = pieces[i]
    out.append("  // Polycube No %d (%d cubes, %d orientations)\n" % (i, len(p["cubes"]), len(orient[i])))
    for (x, y, z) in p["cubes"]:
        out.append("  allPolyCube[%d].AddCube(%d,%d,%d);\n" % (i, x, y, z))
    h, l, flat, basic = p["info"]
    out.append("  allPolyCube[%d].SetInfo(%d,%d,%s,%s);\n" %
               (i, h, l, "TRUE" if flat else "FALSE", "TRUE" if basic else "FALSE"))
    out.append("  allPolyCube[%d].SetGeometry(cubeSide,org,transparent);\n\n" % i)

out.append("}\n")

os.makedirs(os.path.dirname(DST), exist_ok=True)
open(DST, "w", encoding="latin-1").write("".join(out))
print("generato %s: %d pezzi" % (DST, len(pieces)))
tot = sum(len(pieces[i]["cubes"]) for i in range(41))
print("cubi totali: %d" % tot)
