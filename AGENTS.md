# 3DS Homebrew — note di progetto (letto automaticamente da OpenCode)

## Audio NDSP (sintetizzato, niente file esterni)

Sorgenti di riferimento:
- `project/runner-3ds/source/audio.{h,c}` — versione completa: loop musicale
  (2 tracce) + SFX. Usa `audio_set_music()` / SELECT per il toggle.
- `project/blockout-3ds/source/audio.{h,c}` — variante SOLO effetti
  (musica rimossa, canali SFX 0..3, `audio_toggle()` per on/off live).
  Per nuovi giochi senza musica, copiare questa (più recente e senza
  riferimenti a file inesistenti).

Nessuna libreria extra: basta `-lctru` (ndsp è in libctru). Nessun asset:
tutto il PCM è sintetizzato a init in RAM.

### Regole NDSP critiche (fonte: `libctru/source/ndsp/ndsp-channel.c`)

Violarle = silenzio totale SENZA errori. Verificate sul runner:

1. `ndspChnWaveBufAdd()` ignora il buffer se `status` è QUEUED/PLAYING o se
   `nsamples == 0`. Quindi ogni `ndspWaveBuf` parte da
   `memset(..., 0, ...)` (= `NDSP_WBUF_FREE`). **MAI** pre-impostare QUEUED.
2. **Un `ndspWaveBuf` dedicato per ogni canale.** Condividerne uno corrompe
   la coda (`buf->next`) del canale precedente.
3. Buffer PCM in **linear memory** (`linearMemAlign(size, 0x40)`) +
   `DSP_FlushDataCache()` prima di accodare (il DSP legge la RAM con MMU propria).
4. Mix stereo come l'esempio devkitPro `3ds/audio/streaming`:
   `float mix[12] = {1.0f, 1.0f, ...}` passato a `ndspChnSetMix()`.

### Sequenza di init (`audio_init`)

```
ndspInit() → SetOutputMode(STEREO) → SetOutputCount(2) → SetMasterVol()
→ linearMemAlign per ogni buffer → osc_init() → genera PCM → Flush
→ per ogni canale: SetFormat(STEREO_PCM16) + SetInterp(LINEAR) + SetRate(SR) + SetMix()
```

SR = 22050. Canale 0 = musica (solo runner), canali 1..N = SFX round-robin.

### Playback SFX one-shot

```
trova slot libero (status != QUEUED/PLAYING, round-robin) → se nessuno, skip
→ ndspChnWaveBufClear(ch) → memset wavebuf → gen_sfx() nel buffer
→ data_pcm16 + nsamples + looping=false → DSP_FlushDataCache → ndspChnWaveBufAdd()
```

Se si rigenera il PCM a ogni play (come qui), il flush deve coprire
l'intero buffer: size allineata a 64 byte e multipla di 8.

### Helper di sintesi (copiati in entrambi i motori)

- Oscillatore: tabella `sin` 4096 punti + interpolazione lineare
  (niente `sinf`/`exp` per campione: troppo lenti su ARM11; decay via
  moltiplicatore per campione).
- `add_tone` (kind 0 lead / 1 basso / 2 quadra morbida, con pan),
  `add_sweep` (sweep f0→f1), `add_noise` (rumore passa-alto con decay),
  `add_kick` (cassa), `mix_pan` (guadagni pan senza cambio volume percepito).

### Musica (solo runner)

2 tracce da 64 ottavi (~13.7 s), `looping=true`, avviate con
`music_start_now()` su `CH_MUSIC`. `audio_start()` fa partire jingle + musica.

### Debug "non si sente niente"

1. `audio_ok()` su schermo inferiore (entrambi i giochi lo mostrano):
   false = `ndspInit()` fallita (DSP mancante, es. emulatore senza DSP dump).
2. Controllare nell'ordine: memset prima di Add? wavebuf condiviso?
   buffer in linear memory? Flush prima di Add? `nsamples > 0`?
3. `ndspGetDroppedFrames()` su schermo inferiore: se cresce, il mix è troppo
   pesante o i buffer troppo corti.

## Rendering C2D + stereoscopia (3DS, 2026-09)

Riferimenti: `project/blockout-3ds/source/render.{h,c}` e
`project/runner-3ds/source/main.c` (linee 36-180).

### Init corretto di C2D per stereo

```c
gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, false);   /* 3 argomenti! */
C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
C2D_Init(C3D_DEFAULT_CMDBUF_SIZE);
C2D_Prepare();
g_top = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
gfxSet3D(true);                                /* abilita stereo 3D */
```

Errori noti:
- `gfxInit(GFX_BOTTOM | GFX_TOP, GFX_LEFT)` (2 arg) = "too few arguments
  to function 'gfxInit'" — la firma moderna richiede
  `GSPGPU_FramebufferFormat topFormat, GSPGPU_FramebufferFormat bottomFormat, bool vrambuffers`.
- `C3D_RenderTarget` richiede `<citro3d.h>` (non basta `<citro2d.h>`).

### Testo: serve il text buffer

`C2D_DrawText` NON prende `const char *` ma `const C2D_Text *`. Pattern:

```c
C2D_TextBuf g_textBuf = C2D_TextBufNew(4096);  /* in render_init */
C2D_TextBufClear(g_textBuf);                  /* ogni frame prima di parsare */

static void draw_text(int x, int y, float scale, u32 col, const char *s)
{
    C2D_Text txt;
    C2D_TextFontParseLine(&txt, g_font, g_textBuf, s, 0);
    C2D_DrawText(&txt, 0, (float)x, (float)y, 0.0f, scale, scale, col);
}
```

`g_font = C2D_FontLoadSystem(CFG_REGION_USA);` richiede il system font.

### Proiezione 3D software + parallel-shift (comodo)

Pattern `runner-3ds/source/main.c:35-130`:
- Camera con `V3 eye/fwd/right/up`, proiezione `x = OX + dot(v,right)*FOCAL/d`,
  `y = OY - dot(v,up)*FOCAL/d`.
- Stereo: `sh = g_s * FOCAL * (1/D_SCREEN - 1/d)` con `D_SCREEN < min(d)`
  cosi' il contenuto e' sempre "dietro il vetro" (parallel-shift comodo).
- `g_s = ±EYE_FRAC * parallax` per occhio sinistro/destro.
- Cap `MAX_DISP` in pixel (es. 6) per evitare disagio.
- `gfxSet3D(true)` + render una volta per occhio, cambi `g_s` tra le due.

### `circlePosition` e `hidCircleRead` su devkitPro recente

```c
circlePosition cstick;        /* tipo minuscolo! */
hidCircleRead(&cstick);
float cx = (float)cstick.dx / 163.0f;
float cy = (float)cstick.dy / 163.0f;
```

- Il typedef e' `circlePosition` (lowercase), non `CirclePosition`.
- I campi sono `dx`/`dy`, non `x`/`y`.
- `CONFIG_STEREO` per lo slider 3D NON esiste in alcune build recenti di
  libctru; meglio tenere la parallasse fissa al massimo e permettere solo
  `gfxSet3D(true)`.

### `s32` su 3DS e' `long int`

In `printf`/`snprintf` su 3DS, `s32` (cioe' `int32_t`) e' `long int`, NON
`int`. Usare `%ld` / `%6ld`, NON `%d` / `%6d`, altrimenti warning
`-Wformat=` (e portabilita' rotta su toolchain future).

### Strutture con array nested in C: attenzione all'inizializzazione

`typedef struct { s8 n; s8 x[5], y[5], z[5]; } PieceOffset;` si inizializza
cosi' (ogni brace e' un campo, NON un "triplo"):

```c
static const PieceOffset SHAPES[41] = {
    /* 0 (1) */ { 1, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
    /* 1 (2) */ { 2, { 0, 0, 0, 0, 0 }, { 0, 1, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
    ...
};
```

`{ { 0, 0, 0 }, { 0, 1, 0 } }` viene interpretato come il PRIMO elemento
di `x[5]` = una tripla `{x,y,z}` ma `x` e' `s8` non una tripla -> warning
"excess elements in scalar initializer" e l'inizializzazione e' SBAGLIATA
(solo `x[0]` viene riempito, gli altri array restano a zero). Usare
l'inizializzazione "flat" sopra.
