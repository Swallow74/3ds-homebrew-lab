/* TexGen: texture RGBA8 procedurali generate a runtime (niente file, niente
 * romfs).  Sono "maschere di forma": RGB = bianco, la forma (e la luminosita')
 * sta nel canale alpha.  Il colore lo decide il tint di citro2d (blend = 1.0
 * => rgb = colore del tint, alpha = alpha della texture * alpha del tint).
 *
 * Tutte le forme sono simmetriche rispetto all'asse orizzontale: cosi' anche
 * se la convenzione verticale delle UV fosse opposta a quella di tex3ds, il
 * risultato disegnato non cambia. */
#pragma once
#include <citro2d.h>

typedef enum {
	TX_DOT,      /* alone morbido round      32x32 */
	TX_RING,     /* anello luminoso          32x32 */
	TX_STRIPE,   /* strisce di pericolo 45   32x32 */
	TX_PANEL,    /* pannello con griglia     64x64 */
	TX_WIN,      /* finestra illuminata      32x32 */
	TX_STAR,     /* scintilla a 4 punte      16x16 */
	TX_SHADE,    /* ombra a ellisse          32x16 */
	TX_FLAME,    /* fiamma/propulsore        16x32 */
	TX_MARK,     /* rombo di segnalamento    32x32 */
	TX_VISOR,    /* visiera a feritoia       64x16 */
	TX_HULL,     /* piastre scafo            64x64 */
	TX_CHEV,     /* chevron di pericolo      64x32 */
	TX_WALLT,    /* paratia muro + lame      64x64 */
	TX_NUM
} TexId;

bool      texgen_init(void);   /* false se una C3D_TexInit e' fallita */
void      texgen_exit(void);
C2D_Image texgen_get(TexId id);
