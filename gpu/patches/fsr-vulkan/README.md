# Local changes to gpu/third_party/fsr-vulkan

Pinned at upstream FireBurn/FSR-Vulkan `c64f093` (`gpu/deps.txt`). `tools\build.ps1` applies these
patches to the fetched tree when they are not applied yet:

- `0002-...`: `#include <mutex>` in the FFX Vulkan backend; libc++ (MSYS2 CLANG64) does not include it
  transitively. From medievil_pc.
