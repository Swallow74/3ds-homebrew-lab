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

- **Grafica**: poligoni con texture procedurali mappate sulle facce
  (atlas 256x256 con mipmap generato a runtime, `texgen.c`), luce
  direzionale, occlusione verso terra e nebbia per vertice, glow additivi.
  I vertici vanno direttamente nel batch di citro2d (`rx.c`, scritto per
  citro2d 1.7.0; con layout diverso ripiega su triangoli a tinta unita).
- **Scenario synthwave**: sole a strisce, montagne wireframe, palazzi con
  finestre, insegne al neon e antenne, lampioni con pozze di luce.
- **Monete**: file nelle corsie libere e archi sopra le barriere; catena
  sonora a tono crescente. Totale monete salvato su SD.
- **Power-up**: MAGNETE (8 s), SCUDO (assorbe un urto), 2X (10 s).
- **Punteggio** (x difficolta' x moltiplicatore): 1/m, moneta 10, salto
  pulito 25, schivata 2, power-up 50. Moltiplicatore +1 ogni 20 monete
  (max x5), raddoppiato dal 2X.
- **UI**: menu a lista, conto alla rovescia, pausa (START o tocco),
  schermata risultati (A riprova, B titolo), schermo basso con statistiche.
- **3D stereoscopico** comodo (tutto dietro il vetro, HUD sul vetro).
- **Audio DSP** sintetizzato: musica a loop + effetti. SELECT = musica.

Comandi: Sx/Dx corsia, A/B/Su salto, Giu' in aria = picchiata,
START pausa, SELECT musica.

## Licenza

GNU GPL v2 o successiva (vedi `COPYING`). Copyright (C) 2026 Alessandro Del Rosso.
