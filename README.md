# Dragon's Crown Pro — native Windows port (research)

Private research project: run the PS4 release of *Dragon's Crown Pro* natively on Windows x86-64. The game's
original x86-64 code runs directly on the host CPU, a replacement runtime stands in for the PS4 system libraries, and a
shadPS4-derived video core is planned to translate the GPU work to Vulkan. This is not static recompilation and not an
emulator.

| | |
|---|---|
| Title | CUSA10454, content ID `UP2611-CUSA10454_00-DRAGONSCROWNPRO1` |
| Version | `VERSION 01.03` (`APP_VER 01.00`), single base package |
| Status | **Phase 2, first boot**: the game runs to its title screen through the loader and the shadPS4-derived video core ([docs/FIRST_BOOT.md](docs/FIRST_BOOT.md)). |

**No game files are in this repository.** Supply your own dump as a fake PKG:

```
game\Dragons.Crown.Pro.PS4-DUPLEX.pkg
```

`game\`, `user\`, `build\`, `dist\`, `cache\` and `logs\` are git-ignored. The tools never modify the `.pkg`.

## Quick start (PowerShell)

```powershell
tools\setup.ps1 -InstallPython   # checks prerequisites; creates .venv with the one package needed
tools\inspect-game.ps1           # read-only PKG metadata and profile check
tools\build.ps1                  # prepares what is stale (video core deps, game, boot image), then CMake + Ninja, incrementally
tools\build.ps1 -Run             # the same, then launches the build (the launcher first); log in build\logs\dragons_crown_pro_NNN.log
tools\test.ps1                   # focused launcher tests (synthetic data, no window)
tools\clean.ps1                  # remove generated build output only
```

See [docs/LAUNCHER.md](docs/LAUNCHER.md) for the launcher, [docs/BOOTSTRAP.md](docs/BOOTSTRAP.md) for metadata, layout,
import inventory and the first-boot hand-off, and [docs/REFERENCES.md](docs/REFERENCES.md) for why this architecture.

## Layout

| Path | Purpose |
|---|---|
| `tools\` | PowerShell entry points |
| `scripts\` | Python: PKG inspection/unpacking, title profile, boot image, import names and inventory |
| `src\` | C: loader, PS4 runtime (`runtime_*.c`), Windows compat, video core interface (`dcpgpu.h`) |
| `launcher\` | C++: the launcher (TRG Launcher, SDL3, Dear ImGui) the loader opens first; game file check, settings |
| `packaging\` | application icon (SVG, PNGs, ICO) and third-party license notes |
| `tests\` | focused launcher tests |
| `gpu\` | Video core: vendored shadPS4 renderer/recompiler/Gnm/VideoOut + shim (`gpu/VENDOR.txt`); pinned deps in `gpu/deps.txt` |
| `docs\` | Engineering notes |

## License

GNU GPL v2 or later ([LICENSE](LICENSE)). Loader, runtime and scripts are adapted from GPL-2.0 projects listed in
[docs/REFERENCES.md](docs/REFERENCES.md) (uncharted-ndc-native, medievil_pc, gow3_pc, bloodborne_pc, shadPS4);
`scripts/pkg_extract.py` and `pkg_keys.py` follow shadPS4's PKG code. Not affiliated with Sony Interactive
Entertainment, Atlus, Vanillaware or Nippon Ichi Software.
