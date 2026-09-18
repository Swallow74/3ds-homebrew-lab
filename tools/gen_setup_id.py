#!/usr/bin/env python3
"""
gen_setup_id.py - estrae la tabella `setupId[]` (975 valori) da
dl/BL_SRC/BlockOut/SetupManager.cpp e la scrive in
project/blockout-3ds/source/setupId_table.h  (esenza trascrizione manuale).
"""

import re
import os

s = open('dl/BL_SRC/BlockOut/SetupManager.cpp', encoding='latin-1').read()
m = re.search(r'const int setupId\[\] = \{(.*?)\};', s, re.S)
nums = [int(x.strip()) for x in m.group(1).split(',') if x.strip()]
assert len(nums) == 975, len(nums)

out = ["""/*
  File:        setupId_table.h
  Description: Tabella setupId copiata ESATTAMENTE da SetupManager.cpp di
               BlockOut II 2.5 (GPL): converte le 975 combinazioni possibili
               in 585 configurazioni (w,h == h,w sono lo stesso setup).
               Generata da tools/gen_setup_id.py
*/

#ifndef _SETUPIDTABLEH_
#define _SETUPIDTABLEH_

const int setupId[] = {"""]

line = "    "
for i, n in enumerate(nums):
    frag = "%3d" % n + ("," if i < len(nums) - 1 else "")
    if len(line) + len(frag) + 1 > 78:
        out.append(line.rstrip())
        line = "    "
    line += frag + " "
out.append(line.rstrip())
out.append("};")
out.append("#endif /* _SETUPIDTABLEH_ */")
out.append("")

dst = 'project/blockout-3ds/source/setupId_table.h'
os.makedirs(os.path.dirname(dst), exist_ok=True)
open(dst, 'w').write("\n".join(out))
print("ok %d valori -> %s" % (len(nums), dst))
