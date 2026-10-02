# 3DS Homebrew — project notes (read automatically by OpenCode)

## NDSP audio (synthesized, no external files)

Reference sources:
- `project/runner-3ds/source/audio.{h,c}` — full version: music loop
  (2 tracks) + SFX. Uses `audio_set_music()` / SELECT as the toggle.
- `project/blockout-3ds/source/audio.{h,c}` — SFX-ONLY variant
  (music removed, SFX channels 0..3, `audio_toggle()` for live on/off).
  For new games without music, copy this one (newer and without
  references to nonexistent files).

No extra libraries: `-lctru` is enough (ndsp is part of libctru). No assets:
all PCM is synthesized in RAM at init.

### Critical NDSP rules (source: `libctru/source/ndsp/ndsp-channel.c`)

Breaking them = total silence WITHOUT errors. Verified on the runner:

1. `ndspChnWaveBufAdd()` ignores the buffer if `status` is QUEUED/PLAYING or if
   `nsamples == 0`. So every `ndspWaveBuf` starts from
   `memset(..., 0, ...)` (= `NDSP_WBUF_FREE`). **NEVER** pre-set QUEUED.
2. **One dedicated `ndspWaveBuf` per channel.** Sharing one corrupts
   the previous channel's queue (`buf->next`).
3. PCM buffers in **linear memory** (`linearMemAlign(size, 0x40)`) +
   `DSP_FlushDataCache()` before queueing (the DSP reads RAM with its own MMU).
4. Stereo mix like the devkitPro example `3ds/audio/streaming`:
   `float mix[12] = {1.0f, 1.0f, ...}` passed to `ndspChnSetMix()`.

### Init sequence (`audio_init`)

```
ndspInit() → SetOutputMode(STEREO) → SetOutputCount(2) → SetMasterVol()
→ linearMemAlign for each buffer → osc_init() → generate PCM → Flush
→ for each channel: SetFormat(STEREO_PCM16) + SetInterp(LINEAR) + SetRate(SR) + SetMix()
```

SR = 22050. Channel 0 = music (runner only), channels 1..N = round-robin SFX.

### One-shot SFX playback

```
find a free slot (status != QUEUED/PLAYING, round-robin) → if none, skip
→ ndspChnWaveBufClear(ch) → memset wavebuf → gen_sfx() into the buffer
→ data_pcm16 + nsamples + looping=false → DSP_FlushDataCache → ndspChnWaveBufAdd()
```

If the PCM is regenerated on every play (as here), the flush must cover
the whole buffer: size aligned to 64 bytes and a multiple of 8.

### Synthesis helpers (copied in both engines)

- Oscillator: 4096-point `sin` table + linear interpolation
  (no per-sample `sinf`/`exp`: too slow on the ARM11; decay via a
  per-sample multiplier).
- `add_tone` (kind 0 lead / 1 bass / 2 soft square, with pan),
  `add_sweep` (f0→f1 sweep), `add_noise` (high-passed noise with decay),
  `add_kick` (kick drum), `mix_pan` (pan gains without a perceived volume change).

### Music (runner) — `runner-3ds/source/music.{h,c}`

REAL-TIME sequencer + synth (no longer pre-rendered loops): its own thread
(priority main-1, core -2) woken by `ndspSetCallback` via
`LightEvent`, filling 3 rotating wavebufs of 1024 frames on `CH_MUSIC`
(~12 KB of linear memory).  Scores as strings: chords per bar +
melody `"E5 4 A5 4 ..."` (durations in sixteenths, `r` = rest); the section
style generates FM bass, arpeggio, pad, drums and fills.
API: `audio_music(MUS_TITLE/MUS_RUN/MUS_OVER)`, `audio_duck()`,
`audio_set_music()`.  Exit: `music_exit()` (thread join) BEFORE
`ndspExit()`.  PC test: compile `music.c` with `-DMUSIC_HOST` and use
`music_render()` to write a WAV (`music_check()` = 0 if the scores
add up to whole bars). `blockout-3ds` has its own `music.{h,c}` built the same way.

### Debugging "I can't hear anything"

1. `audio_ok()` on the bottom screen (both games show it):
   false = `ndspInit()` failed (missing DSP, e.g. emulator without a DSP dump).
2. Check in order: memset before Add? shared wavebuf?
   buffer in linear memory? Flush before Add? `nsamples > 0`?
3. `ndspGetDroppedFrames()` on the bottom screen: if it grows, the mix is too
   heavy or the buffers too short.

## C2D rendering + stereoscopy (3DS, 2026-09)

References: `project/blockout-3ds/source/render.{h,c}` and
`project/runner-3ds/source/main.c` (lines 36-180).

### Correct C2D init for stereo

```c
gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, false);   /* 3 arguments! */
C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
C2D_Init(C3D_DEFAULT_CMDBUF_SIZE);
C2D_Prepare();
g_top = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
gfxSet3D(true);                                /* enable stereo 3D */
```

Known errors:
- `gfxInit(GFX_BOTTOM | GFX_TOP, GFX_LEFT)` (2 args) = "too few arguments
  to function 'gfxInit'" — the modern signature requires
  `GSPGPU_FramebufferFormat topFormat, GSPGPU_FramebufferFormat bottomFormat, bool vrambuffers`.
- `C3D_RenderTarget` requires `<citro3d.h>` (`<citro2d.h>` is not enough).

### Text: you need the text buffer

`C2D_DrawText` does NOT take a `const char *` but a `const C2D_Text *`. Pattern:

```c
C2D_TextBuf g_textBuf = C2D_TextBufNew(4096);  /* in render_init */
C2D_TextBufClear(g_textBuf);                  /* every frame before parsing */

static void draw_text(int x, int y, float scale, u32 col, const char *s)
{
    C2D_Text txt;
    C2D_TextFontParseLine(&txt, g_font, g_textBuf, s, 0);
    C2D_DrawText(&txt, 0, (float)x, (float)y, 0.0f, scale, scale, col);
}
```

`g_font = C2D_FontLoadSystem(CFG_REGION_USA);` requires the system font.

### Software 3D projection + parallel-shift (comfortable)

Pattern `runner-3ds/source/main.c:35-130`:
- Camera with `V3 eye/fwd/right/up`, projection `x = OX + dot(v,right)*FOCAL/d`,
  `y = OY - dot(v,up)*FOCAL/d`.
- Stereo: `sh = g_s * FOCAL * (1/D_SCREEN - 1/d)` with `D_SCREEN < min(d)`
  so the content is always "behind the glass" (comfortable parallel-shift).
- `g_s = ±EYE_FRAC * parallax` for the left/right eye.
- Cap `MAX_DISP` in pixels (e.g. 6) to avoid discomfort.
- `gfxSet3D(true)` + render once per eye, changing `g_s` between the two.

### `circlePosition` and `hidCircleRead` on recent devkitPro

```c
circlePosition cstick;        /* lowercase type! */
hidCircleRead(&cstick);
float cx = (float)cstick.dx / 163.0f;
float cy = (float)cstick.dy / 163.0f;
```

- The typedef is `circlePosition` (lowercase), not `CirclePosition`.
- The fields are `dx`/`dy`, not `x`/`y`.
- `CONFIG_STEREO` for the 3D slider does NOT exist in some recent libctru
  builds; better to keep the parallax fixed at the maximum and only allow
  `gfxSet3D(true)`.

### `s32` on the 3DS is `long int`

In `printf`/`snprintf` on the 3DS, `s32` (i.e. `int32_t`) is `long int`, NOT
`int`. Use `%ld` / `%6ld`, NOT `%d` / `%6d`, otherwise `-Wformat=`
warnings (and broken portability on future toolchains).

### Structs with nested arrays in C: watch the initialization

`typedef struct { s8 n; s8 x[5], y[5], z[5]; } PieceOffset;` is initialized
like this (each brace is a field, NOT a "triple"):

```c
static const PieceOffset SHAPES[41] = {
    /* 0 (1) */ { 1, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
    /* 1 (2) */ { 2, { 0, 0, 0, 0, 0 }, { 0, 1, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
    ...
};
```

`{ { 0, 0, 0 }, { 0, 1, 0 } }` is interpreted as the FIRST element
of `x[5]` = an `{x,y,z}` triple but `x` is `s8`, not a triple -> warning
"excess elements in scalar initializer" and the initialization is WRONG
(only `x[0]` gets filled, the other arrays stay zero). Use the "flat"
initialization above.

### Sutherland-Hodgman clipping in float: clamp `t` to [0,1]

`t = (e-va)/(vb-va)` with an edge nearly parallel to the border (denominator
in the ulp range) blows up from roundoff and the interpolated vertex ends up at 1e12:
on the PICA it becomes a "giant strip" across the screen. Symptom:
after a while (full pit = thousands of clips/frame) the 3D goes haywire but
the game and HUD stay alive. Fix: `if (denom == 0) skip` + clamp `t` to
`[0,1]` (the true t is always in there). Liang-Barsky for lines is
self-correcting (the point lands on the border by construction) and is not needed.

## Lessons from BlockOut 3DS (2026-09)

- **C2D object limit per FRAME**: `C2D_Init(n)` sizes the vertex
  buffer for the whole frame (it is only emptied at `C3D_FrameEnd`), not per
  flush. Two eyes + bottom screen + many primitives = over 4096 and the excess
  primitives vanish without errors. BlockOut uses 24000.
- **C2D image tint ignored** (verified in Azahar, both
  `C2D_TintSolid` and `C2D_TintMult`): for colored bitmap text, create a
  font texture per color (cached). Ready-made 8x8 font: libctru's `default_font_bin`
  (`extern "C" const u8 default_font_bin[];`, 256 CP437 glyphs,
  MSB = left pixel). RGBA8 128x128 texture, 8x8 morton tiles, memory row 0
  = v 1.0 (top) -> no flip; u32 texel = R<<24|G<<16|B<<8|A;
  `GPU_NEAREST` filter + integer coordinates = crisp text.
- **Sign of the stereo disparity**: behind the screen = NOT crossed
  images (left eye further left). With
  `disp = px * (1 - conv/d)` do `x += (eye ? +0.5 : -0.5) * disp`.
  Scale `px` with `osGet3DSliderState()`.
- **Emulator verification without touching the user**: capture the Azahar
  window by ID (`CGWindowListCopyWindowInfo` -> `screencapture -l <id>`),
  launch with `open -g`, and drive the game with an autotest build
  (`make AT=1` in blockout-3ds). NEVER simulated keys/clicks: they end up in
  the user's active window.
- `make` without `source tools/env.sh` fails with a message that doesn't contain
  the word "error": check that "built ..." appears.
