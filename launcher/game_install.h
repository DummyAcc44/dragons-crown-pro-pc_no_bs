// Installs the game from the player's own fake PKG (or an already extracted game folder): checks it, unpacks or lays
// it out in staging folders next to the prepared game, builds the boot image, checks the result and only then swaps it
// in. Settings, saves, logs and the shader cache live elsewhere and are never touched.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "game_files.h"

namespace dcp {

enum class InstallStatus {
    kInstalled,
    kCancelled,
    kNotPackage,
    kUnsupportedPackage,
    kNotGameFolder,
    kWrongGame,
    kWrongVersion,
    kIncomplete,
    kReadFailed,
    kOverlapping,
    kForeignFolder,
    kNoSpace,
    kWriteFailed,
    kPrepareFailed,
};

struct InstallResult {
    InstallStatus status = InstallStatus::kInstalled;
    std::string detail; // for the log, not the UI
    uint64_t bytes = 0;
    size_t files = 0, linked = 0;
    bool ok() const { return status == InstallStatus::kInstalled; }
};

struct InstallHooks {
    std::function<void(const char* phase)> phase;
    // Returns false to cancel. Cancelling is possible until the new files are activated.
    std::function<bool(uint64_t done, uint64_t total)> progress;
};

// On any failure or cancellation the current installation in `target` is left as it was. InstallGame takes a .pkg
// file or a game folder.
InstallResult InstallGame(const std::filesystem::path& source, const GamePaths& target, const InstallHooks& hooks = {},
                          const GameFingerprint& expected = kSupportedGame);
InstallResult InstallFromPackage(const std::filesystem::path& package, const GamePaths& target,
                                 const InstallHooks& hooks = {}, const GameFingerprint& expected = kSupportedGame);
InstallResult InstallFromFolder(const std::filesystem::path& source, const GamePaths& target,
                                const InstallHooks& hooks = {}, const GameFingerprint& expected = kSupportedGame);

struct InstallMessage {
    const char* title;
    const char* text;
};
InstallMessage DescribeInstall(InstallStatus status);

} // namespace dcp
