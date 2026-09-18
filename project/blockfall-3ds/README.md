# Blockfall 3DS (.3dsx)

Falling-blocks puzzle homebrew per Nintendo 3DS con grafica GPU (citro2d):
blocchi colorati con highlight, ghost piece, preview prossimo pezzo,
punteggio/livello su schermo superiore, aiuto su schermo inferiore.

```bash
source ../../tools/env.sh
make        # output/blockfall-3ds.3dsx + .smdh
make clean
```

Installazione su 3DS: copia `output/blockfall-3ds.3dsx` e `output/blockfall-3ds.smdh`
in `sd:/3ds/blockfall-3ds/`, avvia da Homebrew Launcher (hbmenu).

Comandi: D-Pad muovi / Giu veloce, A/Su ruota, B hard drop, START esci.
A su GAME OVER = ricomincia. Punteggio stile guideline
(100/300/500/800 × livello, livello ogni 10 linee), bag 7 pezzi.
