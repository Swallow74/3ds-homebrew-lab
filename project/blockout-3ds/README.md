# BlockOut 3DS (port non ufficiale)

Porting **fedele e non ufficiale** di *BlockOut II 2.5* (GPL 2007, Jean-Luc Pons,
https://www.blockout.net/blockout2/) per Nintendo 3DS (devkitARM / libctru /
citro2d). "BlockOut" e' un marchio registrato di Kadon Enterprises, usato qui
solo per identificare il gioco originale. Licenza del port: GNU GPL v2 o
successiva (vedi `COPYING`). Le regole, le tabelle dei polycubi, le formule di punteggio, i tempi
di gioco, l'IA del bot e i file di setup/save sono quelli originali: sono
stato adattato solo ciò che l'hardware 3DS richiede (sorgente input, backend
grafico, audio, filesystem).

Sorgente di riferimento estratto in `dl/BL_SRC/BlockOut/` (BlockOut II 2.5,
124 file).

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
| `main.cpp`     | init hardware, loop `aptMainLoop`, mapping pad→tasti, menu (6 modalità), pagina configurazione, pagina punteggi |
| `render.cpp`   | renderer software 2D: proiez. con la matrice originale (`GLMatrix`/`GLCamera`), illuminazione per facce,立体 HUD, pozzo, pezzo, spark |
| `audio.c`      | NDSP: 4 canali effetti + 1 canale musica loop (tutto sintetizzato, stessa "family" di suoni dell'originale) |
| `Game.cpp`     | invariato: regole, pit, punteggio, timer, modalità (play/practice/demo/setup) |
| `Pit.*`, `Piece.*`, `Cube.*`, `BotPlayer*`, `APlayer.*` | invariati |
| `SetupManager.*` | setup + high score su SD (`/3ds/blockout/`) |
| `bo_compat.h`  | codici tasto `BO_KEY_*` (evita i conflitti con i bit `KEY_*` di libctru) |

## Comandi

### Menu (6 voci, come l'originale)
`su/giù` seleziona · `A`/`START` conferma · `B` esce · `SELECT` esci

### Gioco
| Tasto | Azione |
|-------|--------|
| D-Pad | muove il pezzo nel piano 5×5 (`L` premuto: diagonali = ruota il pozzo: HOME/END/PAGEUP/PAGEDOWN) |
| `A`   | space (discesa di una cella) |
| `B` / `X` / `Y` | ruota il pezzo sugli assi Z1/X1/Y1 (con `R` premuto: Z2/X2/Y2) — stessi assi della pagina *Configurazione* |
| `L`+D-Pad | ruota il **pozzo** (pitch/yaw/twist, come i tasti HOME/END/PAGEUP/PAGEDOWN originali) |
| `ZL`  | scatta foto (spark) |
| `ZR`  | cambia la separazione stereoscopica (0.01 … 0.11) |
| `START` | pausa (`RETURN` a fine partita) |
| `SELECT` | esci (menu) |
| `L`+`R`+`START` | audio on/off |
| C-Stick su | HUD on/off (tasto `H` dell'originale) |

### Pagina configurazione
Modifica i valori con su/giù (e sinistra/destra per i campi ampi); `A`
salva su `/3ds/blockout/setup.dat` e applica subito le dimensioni del pozzo.

## Adattamenti (solo hardware)

- Input: tastiera SDL → pad 3DS (`hid`); i codici tasto sono rinominati
  `BO_KEY_*` perché libctru usa `KEY_A/B/L/...`
- Grafica: OpenGL/GDI → citro2d. Le **texture** originali (marmo, vetro,
  cristallo, numeri, sfondi) non sono nel pacchetto sorgente, quindi il
  renderer usa lo stile **CLASSIC** con le tavolozze colore e i materiali
  (diffuse/ambient) identici a `Pit::Create`/`Game::Create`
- Audio: SDL_mixer → NDSP sintetizzato (4 canali effetti + 1 musica in loop)
- Filesystem: `%APPDATA%`/`HOME` → `/3ds/blockout/` (`setup.dat`, `hscore.dat`)
- Lo schermo superiore è il pozzo (400×240, stereoscopico), quello inferiore
  è la colonna del livello/punteggi (come nell'originale)

## Stato / da verificare

## Diagnostica di avvio

All'avvio viene mostrato per ~2 s un frame di test (barre rosso/verde/blu +
testo **BOOT OK** su entrambi gli schermi, voce `BOOT_SELFTEST` in
`main.cpp`). Serve a capire dove si ferma il problema:

- **non vedi nulla** → problema di avvio/pipeline grafico di base (non del
  gioco): verificare loader/emulatore e init
- **vedi BOOT OK ma poi schermo vuoto** → il blocco è in `Game::Create` o
  nel rendering del pozzo/HUD
- **vedi BOOT OK e poi il menu** → tutto ok, togli `BOOT_SELFTEST`

Due bug corretti dopo il primo test (schermo completamente vuoto):

### Corretto dopo la seconda prova su target (cubi neri, pezzo invisibile)

- **Materiali dei cubi del pozzo**: `Pit::GetMaterial(level)` restituisce un
  puntatore a un `static` (come nell'originale). Il renderer 3DS costruiva una
  `mats[24]` *prima* del loop di disegno: tutti gli entry puntavano allo stesso
  `static`, quindi ogni cubo usava il colore dell'ultima chiamata (level
  fuori-range → `memset` a zero → **nero**). Ora il materiale viene riletto
  **per ogni cella**, esattamente come fa `Pit::Render` nell'originale.
- **Il pezzo corrente non compariva**: `Game::Update` costruisce `mat` partendo
  da `matView` (fedele all'originale `glLoadMatrixf(matView)`), ma il renderer
  moltiplicava di nuovo la vista (`gView * mat`) → doppia transform, pezzi
  dietro la camera e mai disegnati. Ora `Game` espone anche `matPiece` /
  `matAIPiece` (le stesse matrici **senza** `matView`) e il renderer applica la
  vista per occhio una sola volta.

Nota sulla prospettiva ( identica all'originale): l'estremita' **vicina** del
pozzo (z = 0, dove nasce il pezzo) e' in **basso** sullo schermo ed e' grande;
il fondo del pozzo (z = depth-1) e' **lontano**, al centro e piccolo. Il pezzo
quindi "scende" allontanandosi dalla camera.


- i colori in `render.h` (`COL_*`) avevano **alpha = 0**: con citro2d
  (`C2D_Color32` = R|G<<8|B<<16|A<<24) qualunque oggetto disegnato era
  completamente trasparente → corretto con `OPAQUE()` e alpha `0xFF`
- `bo_compat.h` aveva **GL_OK/GL_FAIL invertiti** (0/-1 invece di 1/0 come
  in `GLApp.h` dell'originale): `Game::StartGame` conteneva
  `if( !Create(...) ) exit(0);` → l'app si chiudeva appena si avviava
  una partita

- [x] Compila pulito: `output/blockout-3ds.3dsx`
- [x] Primo run su target: menu/HUD/geometrica visibili; cubi neri e pezzo
      invisibile (corretti, vedi sopra). Da riverificare: discesa, colori per
      layer, parallasse, audio, file su SD e i flussi menu/pratica/demo
- [ ] Texture di stile (MARBLE/ARCADE) e sfondi: assenti nel sorgente estratto
      (il renderer forza lo stile CLASSIC con le tavolozze originali)
- [ ] `Game::InitializeMaterials` usa `Pit::GetMaterial(level)` (per-colori per
      layer, identico all'originale): i valori sono corretti, ma i materiali
      per stile non-CLASSIC richiedono le texture mancanti
