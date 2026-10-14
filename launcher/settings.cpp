#include "settings.h"

#include <fstream>
#include <system_error>
#include <utility>

#include "dcp_launcher.h"
#include "dcp_resolution.h"
#include "utf8.h"

namespace dcp {
namespace {

constexpr ManagedSetting kManagedSettings[] = {
    {kShowLauncherKey, "1"},
    {kDisplayModeKey, "windowed"},
    {kWindowWidthKey, "1920"},
    {kWindowHeightKey, "1080"},
    {kMonitorKey, "1"},
    {kVSyncKey, "1"},
    {kUpscaleKey, "0"},
    {kSharpenKey, "1"},
    {kSharpenStrengthKey, "75"},
    {kRenderResolutionKey, kNativeResolution},
    {kGraphicsCardKey, kAutomaticGpu},
    {kAntiAliasingKey, "0"},
    {kAnisotropyKey, "0"},
    {kShowFpsTitleKey, "0"},
    {kShowFrameCounterKey, "0"},
    {kLauncherXKey, ""},
    {kLauncherYKey, ""},
    {kLauncherWidthKey, ""},
    {kLauncherHeightKey, ""},
    {kLauncherMaximizedKey, "0"},
    {kLauncherFullscreenKey, "0"},
};

constexpr std::string_view kPlacementKeys[] = {kLauncherXKey,         kLauncherYKey,         kLauncherWidthKey,
                                                kLauncherHeightKey,    kLauncherMaximizedKey, kLauncherFullscreenKey};

constexpr char kFileHeader[] =
    "# Dragon's Crown Pro user settings (the launcher and the in-game menu write this file).\n"
    "# Remove a line to use the default.\n";

} // namespace

std::span<const ManagedSetting> ManagedSettings() {
    return kManagedSettings;
}

bool IsLauncherPlacementSetting(std::string_view key) {
    for (const std::string_view placement_key : kPlacementKeys) {
        if (placement_key == key) {
            return true;
        }
    }
    return false;
}

LauncherSettings::LauncherSettings(std::filesystem::path file) : file_(std::move(file)) {
    true_value = "1";
    false_value = "0";
    const trg::TextFileSettings stored(Utf8(file_));
    for (const ManagedSetting& setting : kManagedSettings) {
        if (stored.Has(setting.key)) {
            saved_[std::string(setting.key)] = stored.Get(setting.key);
        }
    }
    pending_ = saved_;
}

std::string LauncherSettings::Get(std::string_view key, std::string_view def) const {
    const auto it = pending_.find(key);
    if (it == pending_.end() || it->second.empty()) {
        return std::string(def);
    }
    // Exclusive fullscreen belongs to the in-game menu; the launcher offers Windowed and Borderless Fullscreen.
    return key == kDisplayModeKey && it->second == "fullscreen" ? std::string("borderless") : it->second;
}

void LauncherSettings::Set(std::string_view key, std::string value) {
    pending_[std::string(key)] = std::move(value);
}

bool LauncherSettings::Save() {
    Values changes;
    for (const auto& [key, value] : pending_) {
        const auto saved = saved_.find(key);
        if (saved == saved_.end() || saved->second != value) {
            changes.emplace(key, value);
        }
    }
    if (changes.empty()) {
        return true;
    }
    if (!WriteFile(changes)) {
        return false;
    }
    for (const auto& [key, value] : changes) {
        saved_[key] = value;
    }
    return true;
}

bool LauncherSettings::SaveNow(const Values& values) {
    if (!WriteFile(values)) {
        return false;
    }
    for (const auto& [key, value] : values) {
        saved_[key] = value;
        pending_[key] = value;
    }
    return true;
}

void LauncherSettings::ResetToDefaults() {
    for (const ManagedSetting& setting : kManagedSettings) {
        if (!IsLauncherPlacementSetting(setting.key)) {
            pending_[std::string(setting.key)] = std::string(setting.default_value);
        }
    }
}

bool LauncherSettings::WriteFile(const Values& changes) {
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    if (!std::filesystem::exists(file_, ec)) {
        std::ofstream(file_) << kFileHeader;
    }
    trg::TextFileSettings file(Utf8(file_));
    for (const auto& [key, value] : changes) {
        file.Set(key, value);
    }
    return file.Save();
}

bool LauncherWanted(bool game_ready, bool show_setting, bool shift_held, bool skip_once) {
    if (!game_ready) {
        return true;
    }
    return !skip_once && (shift_held || show_setting);
}

} // namespace dcp

extern "C" int dcp_render_resolution(const char* settings_file) {
    using namespace dcp;
    const LauncherSettings settings(PathFromUtf8(settings_file ? settings_file : ""));
    return dcp_resolution_index(settings.Get(kRenderResolutionKey, kNativeResolution).c_str());
}
