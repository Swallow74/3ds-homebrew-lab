# BlockOut 3DS (port non ufficiale)

Porting **fedele e non ufficiale** di *BlockOut II 2.5* (GPL 2007, Jean-Luc Pons,
https://www.blockout.net/blockout2/) per Nintendo 3DS (devkitARM / libctru /
citro2d). "BlockOut" e' un marchio registrato di Kadon Enterprises, usato qui
solo per identificare il gioco originale. Licenza del port: GNU GPL v2 o
successiva (vedi `COPYING`). Le regole, le tabelle dei polycubi, le formule di punteggio, i tempi
di gioco, l'IA del bot e i file di setup/save sono quelli originali: sono
stato adattato solo ciò che l'hardware 3DS richiede (sorgente input, backend
grafico, audio, filesystem).

Il sorgente originale di BlockOut II 2.5 non è incluso nel repo: si scarica da
https://www.blockout.net/blockout2/.

## Build

```bash
cd project/blockout-3ds
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
export PATH=$DEVKITARM/bin:$PATH
make            # → output/blockout-3ds.{elf,3dsx,smdh}
```

Servono le librerie installate: **libctru** (servizi separati: `hid`, `apt`,
`ndsp`), **citro2d/citro3d**. Il vecchio `gfx3d` non esiste più: il renderer
usa `C3D/C2D` con frame buffer sinistro/destro e `gfxScreenSwapBuffers`.

## Struttura

| File | Ruolo |
|------|-------|
| `main.cpp`     | init hardware, loop `aptMainLoop`, mapping pad->tasti, flusso menu/partita, musica per stato |
| `screens.cpp`  | intro (logo 3D a voxel), menu, setup, hall of fame, pausa, fine partita + nome |
| `ui.cpp`       | primitive UI stile DOS: palette EGA, riquadri a doppia linea, barre, menu |
| `render.cpp`   | renderer software 3D (pozzo, cubi, pezzo, orbita GAME OVER), HUD DOS, font bitmap 8x8 |
| `audio.c`      | NDSP: 4 canali effetti sintetizzati (stile DOS a onda quadra o BlockOut II) |
| `music.c`      | colonna sonora in tempo reale (sequencer + synth in thread, canale 4): menu, gioco, game over |
| `Game.cpp`, `Pit.*`, `PolyCube.*`, `BotPlayer*`, `BotMatrix*` | logica originale di BlockOut II 2.5 |
| `SetupManager.*` | setup + high score su SD (`/3ds/blockout/`) |
| `autotest.h`   | `make AT=1`: il gioco si pilota da solo (verifica grafica in emulatore) |

## Comandi

### Menu
`su/giù` seleziona · `A`/`START` conferma · touch sulle voci · inattivita' 40 s = demo

### Gioco
| Tasto | Azione |
|-------|--------|
| D-Pad | muove il pezzo (`L`+D-Pad: diagonali, come 7/9/1/3 del tastierino) |
| `A`   | discesa rapida |
| `B` / `X` / `Y` | rotazione su Z / X / Y (`R` premuto: verso opposto) |
| `ZL`  | suggerimento dell'IA (pratica) |
| `ZR`  | profondita' 3D (OFF, 1..4; segue anche il cursore 3D) |
| `START` | pausa (menu: riprendi / ricomincia / esci); ferma la demo |
| `SELECT` | termina la partita |

### Setup
Pozzo (3..7 x 3..7 x 6..18), set di blocchi, livello iniziale, velocita'
animazioni, facce fantasma, riempimento del pezzo, profondita' 3D, effetti
on/off, stile effetti (MS-DOS / BlockOut II), musica on/off. `B` salva.
Le opzioni del port sono accodate a `setup.dat` (file vecchi compatibili).

## Resa "MS-DOS"

- fondo nero, reticolo verde a 1 pixel, strati colorati per profondita'
  con spigoli neri, pezzo in caduta a solo filo bianco (rosso se bloccato)
- viewport del pozzo quadrato (come l'originale: la proiezione ha aspect 1)
- colonna dei livelli a sinistra, colonna LEVEL / SCORE / CUBES PLAYED /
  HIGH SCORE / PIT / BLOCK SET a destra, font 8x8 CP437, palette EGA
- orbita di GAME OVER con facce ordinate per profondita' (l'originale usava
  lo z-buffer)
- lampo del reticolo quando si completano strati

## Correzioni (revisione 2026-09)

- **IA della demo/pratica**: una versione precedente aveva invertito le
  matrici di `BotMatrix` e cambiato i coefficienti: il bot non completava
  mai uno strato. Ripristinati i sorgenti originali (le matrici di `Game`
  e `GLMatrix` sono identiche all'originale).
- **Stereoscopia invertita**: il segno della disparita' era sbagliato
  (bocca del pozzo "dietro", fondo "davanti"). Ora disparita' zero sulla
  bocca e pozzo dietro lo schermo, HUD sul piano dello schermo.
- **Poligoni mancanti a pozzo pieno**: `C2D_Init(4096)` limita gli oggetti
  per FRAME (due occhi + schermo basso): portato a 24000.
- **Testo illeggibile**: il font di sistema scalato a 0.25 era sfocato;
  ora font bitmap 8x8 con filtro nearest.
- facce del fantasma (`GHOST FACES`) con vicini sbagliati sugli assi X/Y.

## Adattamenti (solo hardware)

- Input: tastiera SDL → pad 3DS (`hid`); i codici tasto sono rinominati
  `BO_KEY_*` perché libctru usa `KEY_A/B/L/...`
- Grafica: OpenGL/GDI → citro2d. Le **texture** originali (marmo, vetro,
  cristallo, numeri, sfondi) non sono nel pacchetto sorgente, quindi il
  renderer usa lo stile **CLASSIC** con le tavolozze colore e i materiali
  (diffuse/ambient) identici a `Pit::Create`/`Game::Create`
- Audio: SDL_mixer -> NDSP sintetizzato (4 canali effetti + musica in streaming)
- Filesystem: `%APPDATA%`/`HOME` -> `/3ds/blockout/` (`setup.dat`, `hscore.dat`)
- Schermo superiore: pozzo + HUD (400x240, stereoscopico); inferiore:
  statistiche, comandi, menu toccabili

## Stato

- [x] Compila pulito: `output/blockout-3ds.3dsx`
- [x] Verificato in Azahar (autotest): intro, menu, setup, hall of fame,
      partita, pausa, game over + nome, pratica con suggerimento, demo che
      completa strati
- [ ] Da provare su console: effetto 3D reale, audio/musica, prestazioni
      su Old 3DS
- [ ] Texture di stile (MARBLE/ARCADE): assenti nel sorgente, resta CLASSIC
