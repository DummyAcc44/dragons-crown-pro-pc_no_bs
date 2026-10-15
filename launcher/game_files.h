// The one readiness check for the game: the launcher, Play and the installer all use it.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "game_profile.h"

namespace dcp {

enum class GameFileState { kMissing, kValid, kDifferentVersion, kReadError };

// The prepared /app0 tree (game\prepared\v1.03) and the loader's boot image (build\image\v1.03).
struct GamePaths {
    std::filesystem::path app0;
    std::filesystem::path image_dir;
};

struct GameCheck {
    GameFileState state = GameFileState::kMissing;
    std::string detail;       // for the log, not the UI
    std::string eboot_sha256; // lower-case hex, set once eboot.bin was read
};

// Reads the preparation marker, param.sfo and the boot image report, and hashes eboot.bin (about 21 MB). The game
// data archives are only checked for presence: the marker records that their extraction finished.
GameCheck CheckGameFiles(const GamePaths& paths, const GameFingerprint& expected = kSupportedGame);

// The game itself, without a preparation marker or boot image: param.sfo, the required files and eboot.bin. The
// installer checks what it unpacked or the folder the player chose with it.
GameCheck CheckGameTree(const std::filesystem::path& app0, const GameFingerprint& expected = kSupportedGame);

// TITLE_ID, CONTENT_ID, APP_VER and VERSION of a param.sfo: the installer checks a package's before unpacking it.
GameCheck CheckSfoIdentity(const std::string& sfo, const GameFingerprint& expected = kSupportedGame);

// A string value of param.sfo, empty when the key is absent or not a string.
std::string SfoString(const std::string& sfo, const char* key);
// An integer value of param.sfo: `absent` when the key is missing, nothing when it is not an integer.
std::optional<uint32_t> SfoInteger(const std::string& sfo, const char* key, uint32_t absent);

// Lower-case hex SHA-256 of a file, empty when it cannot be read.
std::string Sha256Hex(const std::filesystem::path& file);

// The string value of a top-level-looking "key": "value" pair in JSON text; empty when absent.
std::string JsonString(const std::string& text, const std::string& key);

const char* StateName(GameFileState state);

} // namespace dcp
