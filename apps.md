# App del workspace

| Progetto | Titolo | Unica ID | Output | Note |
|---|---|---|---|---|
| `project/_template` | Template | 0x1B111 | `.3dsx` | base di `tools/new-app.sh` |
| `project/blockfall-3ds` | Blockfall 3DS | 0x1B131 | `.3dsx` | falling-blocks, font di sistema |
| `project/blockout-3ds` | Blockout II 3DS | 0x1B132 | `.3dsx` | adattamento GPL di BlockOut II (pozzo 5x5x12, 41 polycubi), 3D stereoscopico, audio NDSP sintetizzato |
| `project/runner-3ds` | Neon Rush 3DS | 0x1B133 | `.3dsx` | runner 3D stereoscopico, mesh procedurale, parallasse "comoda" |
| `project/time-pilot-3ds` | Time Pilot 3DS | 0x1B134 | `.3dsx` | clone di Time Pilot (Konami 1982): 5 epoche, nave madre, 3D stereoscopico, solo effetti (nessuna musica) |

Cartelle condivise: `resources/AppInfo` (titolo/ID/codice prodotto),
`resources/icon.png` (48x48 per lo `.smdh`), `Makefile` (build 3dsx).

Build di una app:

```bash
cd project/<app> && source ../../tools/env.sh && make
```

`.cia`/`.3ds` richiedono `makerom` + `bannertool` (vedi `notes/CIA-vs-3DSX.md`).
