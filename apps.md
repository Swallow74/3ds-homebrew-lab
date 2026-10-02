# Workspace apps

| Project | Title | Unique ID | Output | Notes |
|---|---|---|---|---|
| `project/_template` | Template | 0x1B111 | `.3dsx` | base for `tools/new-app.sh` |
| `project/blockfall-3ds` | Blockfall 3DS | 0x1B131 | `.3dsx` | falling-blocks, system font |
| `project/blockout-3ds` | Blockout II 3DS | 0x1B132 | `.3dsx` | GPL adaptation of BlockOut II (5x5x12 pit, 41 polycubes), stereoscopic 3D, synthesized NDSP audio |
| `project/runner-3ds` | Neon Rush 3DS | 0x1B133 | `.3dsx` | stereoscopic 3D runner, procedural meshes, "comfortable" parallax |

Shared folders: `resources/AppInfo` (title/ID/product code),
`resources/icon.png` (48x48 for the `.smdh`), `Makefile` (3dsx build).

Building an app:

```bash
cd project/<app> && source ../../tools/env.sh && make
```

`.cia`/`.3ds` need `makerom` + `bannertool` (see `notes/CIA-vs-3DSX.md`).
