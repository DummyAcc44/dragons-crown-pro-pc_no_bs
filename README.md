# Dragon's Crown Pro — native Windows port (research)

Private research project: run the PS4 release of *Dragon's Crown Pro* natively on Windows x86-64. The game's
original x86-64 code runs directly on the host CPU, a replacement runtime stands in for the PS4 system libraries, and a
shadPS4-derived video core is planned to translate the GPU work to Vulkan. This is not static recompilation and not an
emulator.

| | |
|---|---|
| Title | CUSA10454, content ID `UP2611-CUSA10454_00-DRAGONSCROWNPRO1` |
| Version | `VERSION 01.03` (`APP_VER 01.00`), single base package |
| Status | **Phase 1, bootstrap**: tooling, preparation, loader/runtime skeleton and import inventory. The loader maps the game and stops at the first unsupported import; no graphics yet. |

**No game files are in this repository.** Supply your own dump as a fake PKG:

```
game\Dragons.Crown.Pro.PS4-DUPLEX.pkg
```

`game\`, `user\`, `build\`, `dist\`, `cache\` and `logs\` are git-ignored. The tools never modify the `.pkg`.

## Quick start (PowerShell)

```powershell
tools\setup.ps1 -InstallPython   # checks prerequisites; creates .venv with the one package needed
tools\inspect-game.ps1           # read-only PKG metadata and profile check
tools\prepare-game.ps1           # unpack (resumable), build the /app0 view and the boot image
tools\build.ps1                  # MSYS2 CLANG64: CMake + Ninja -> build\cmake\clang64\bin\dcp-loader.exe
tools\run.ps1 -CheckImports      # list PS4 imports the runtime lacks, without running the game
tools\run.ps1 -TimeoutSeconds 60 # controlled run; log in user\last_run.log
tools\clean.ps1                  # remove generated build output only
```

See [docs/BOOTSTRAP.md](docs/BOOTSTRAP.md) for metadata, layout, import inventory and the first-boot hand-off, and
[docs/REFERENCES.md](docs/REFERENCES.md) for why this architecture.

## Layout

| Path | Purpose |
|---|---|
| `tools\` | PowerShell entry points |
| `scripts\` | Python: PKG inspection/unpacking, title profile, boot image, import names and inventory |
| `src\` | C: loader, PS4 runtime (`runtime_*.c`), Windows compat, video core interface (`dcpgpu.h`) and stub |
| `docs\` | Engineering notes |

## License

GNU GPL v2 or later ([LICENSE](LICENSE)). Loader, runtime and scripts are adapted from GPL-2.0 projects listed in
[docs/REFERENCES.md](docs/REFERENCES.md) (uncharted-ndc-native, medievil_pc, gow3_pc, bloodborne_pc, shadPS4);
`scripts/pkg_extract.py` and `pkg_keys.py` follow shadPS4's PKG code. Not affiliated with Sony Interactive
Entertainment, Atlus, Vanillaware or Nippon Ichi Software.
