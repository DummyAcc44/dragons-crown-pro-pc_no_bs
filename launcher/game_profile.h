// The supported game build. Mirrors scripts/game_profiles.py; tests/launcher_tests.cpp checks they agree.
#pragma once

#include <string_view>

namespace dcp {

inline constexpr char kGameTitle[] = "Dragon's Crown Pro";
inline constexpr char kPortRepository[] = "github.com/saulob/dragons-crown-pro-pc";

struct GameFingerprint {
    std::string_view title_id;
    std::string_view content_id;
    std::string_view app_ver;      // param.sfo APP_VER
    std::string_view sfo_version;  // param.sfo VERSION
    std::string_view version;      // the profile version, as in game\prepared\v<version>
    std::string_view eboot_sha256; // lower-case hex of the prepared eboot.bin
};

inline constexpr GameFingerprint kSupportedGame{
    "CUSA10454",
    "UP2611-CUSA10454_00-DRAGONSCROWNPRO1",
    "01.00",
    "01.03",
    "1.03",
    "f9bff150c4c28507096e7e143a07492063ed89bf05ac6cb995a55a0946e3508a",
};

// Files every complete game folder has, the player's and the prepared one alike.
inline constexpr const char* kGameFiles[] = {
    "eboot.bin",
    "sce_discmap.plt",
    "DRACRO.CPK",
    "DRACRO_US.CPK",
    "sce_module/libc.prx",
    "sce_module/libSceFios2.prx",
    "sce_module/libSceNpToolkit2.prx",
    "sce_sys/param.sfo",
};

// Linked after the eboot in this order: libc.prx needs libSceFios2.prx.
inline constexpr const char* kBundledModules[] = {"libc.prx", "libSceFios2.prx", "libSceNpToolkit2.prx"};

// Written last into a prepared game folder (scripts/game_view.py writes the same one).
inline constexpr char kPreparedMarker[] = ".dcp_view.json";

} // namespace dcp
