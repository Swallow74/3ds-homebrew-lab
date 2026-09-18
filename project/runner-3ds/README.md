# Neon Rush 3DS (.3dsx)

Runner "into the screen" per Nintendo 3DS: 3 corsie, barriere basse da
saltare, muri alti da schivare, velocita' crescente, 3D stereoscopico.

```bash
source ../../tools/env.sh
make        # output/runner-3ds.3dsx + .smdh
make clean
```

Su 3DS: copia `output/*` in `sd:/3ds/runner-3ds/`, avvia da hbmenu
(il best score si salva in `sd:/3ds/runner-3ds/best.txt`).
Su Mac: apri la `.3dsx` con Azahar.

- **3D stereoscopico**: gli ostacoli vengono DAVVERO verso di te col
  cursore 3D alzato (su Azahar resta 2D).
- **Audio DSP** sintetizzato: loop veloce Em–C–G–D + effetti
  (salto/crash/bonus/cambio corsia). SELECT = musica on/off.
- **Fisica/effetti**: salto con gravita', atterraggio con polvere,
  scie di velocita' oltre soglia, scossa e flash sul crash,
  cubo che si inclina in curva, stelle e griglia prospettica.

Comandi: D-Pad Sx/Dx corsia, A/Su salto, START esci, A su CRASH rigioca.
