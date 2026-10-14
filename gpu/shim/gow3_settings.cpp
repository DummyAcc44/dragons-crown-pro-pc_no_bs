// SPDX-License-Identifier: GPL-2.0-or-later
#include "gow3_settings.h"
#include "gow3_graphics.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Gow3Settings {

namespace {

const char* Path() {
    const char* env = std::getenv("GOW3_CONFIG");
    return env && env[0] ? env : "dcp.ini";
}

float Clamp(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

std::string Trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::mutex file_mutex; // the menu (present thread) and F11 (window thread) both write

using Entries = std::vector<std::pair<std::string, std::string>>;

// Sets each key of `entries` in the file: its line is rewritten in place, a missing one is appended, and every
// other line (the launcher's keys, comments) is kept as it is. `header` starts a file that does not exist yet.
bool WriteEntries(const Entries& entries, const char* header) {
    std::scoped_lock lock{file_mutex};
    std::vector<std::string> lines;
    if (FILE* in = std::fopen(Path(), "r")) {
        char line[512];
        while (std::fgets(line, sizeof(line), in)) {
            lines.push_back(Trim(line));
        }
        std::fclose(in);
    } else if (header) {
        lines.push_back(header);
    }
    std::vector<bool> written(entries.size(), false);
    for (std::string& line : lines) {
        const auto eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) {
            continue;
        }
        const std::string key = Trim(line.substr(0, eq));
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].first == key) {
                line = key + "=" + entries[i].second;
                written[i] = true;
            }
        }
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!written[i]) {
            lines.push_back(entries[i].first + "=" + entries[i].second);
        }
    }
    const std::filesystem::path path{Path()};
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    const std::string temporary = std::string(Path()) + ".tmp";
    FILE* out = std::fopen(temporary.c_str(), "w");
    if (!out) {
        std::printf("Settings: cannot write %s\n", Path());
        return false;
    }
    for (const std::string& line : lines) {
        std::fprintf(out, "%s\n", line.c_str());
    }
    std::fclose(out);
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::printf("Settings: cannot replace %s: %s\n", Path(), ec.message().c_str());
        return false;
    }
    return true;
}

// The "key=value" lines of formatted text (comments skipped).
Entries ParseEntries(const std::string& text) {
    Entries entries;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = text.substr(start, end - start);
        const auto eq = line.find('=');
        if (!line.empty() && line[0] != '#' && eq != std::string::npos) {
            entries.emplace_back(line.substr(0, eq), line.substr(eq + 1));
        }
        start = end + 1;
    }
    return entries;
}

constexpr char FileHeader[] = "# video core settings (in-game menu: Insert / R3+L2)";

void Appendf(std::string& text, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    text += buffer;
}

void Set(Values& v, const std::string& key, const std::string& value) {
    const float f = float(std::atof(value.c_str()));
    const int i = std::atoi(value.c_str());
    static const char* const overlay_keys[] = {"overlay_fps", "overlay_frametime", "overlay_upscaler",
                                               "overlay_gpu", "overlay_cpu", "overlay_ram", "overlay_vram"};
    for (unsigned item = 0; item < 7; ++item) {
        if (key == overlay_keys[item]) {
            const unsigned bit = 1u << item;
            v.overlay_items = i ? (v.overlay_items | bit) : (v.overlay_items & ~bit);
            return;
        }
    }
    if (key == "overlay_scale") {
        v.overlay_scale = std::clamp(i, 50, 300);
        return;
    }
    if (key == "overlay_corner") {
        v.overlay_corner = i >= 0 && i <= 3 ? i : 1;
        return;
    }
    if (key == "overlay_opacity") {
        v.overlay_opacity = std::clamp(i, 0, 100);
        return;
    }
    if (key == "overlay_layout") {
        v.overlay_layout = i == 1 ? 1 : 0;
        return;
    }
    if (key == "menu_tab") {
        v.menu_tab = i >= 0 && i <= 1 ? i : 0;
        return;
    }
    if (key == "upscaler") {
        for (int u = 0; u < UpscalerCount; ++u) {
            if (value == UpscalerName(u)) {
                v.upscaler = u;
            }
        }
    } else if (key == "preset") {
        v.preset = std::clamp(i, 0, PresetCount - 1);
    } else if (key == "sharpen") {
        v.sharpen = i != 0;
    } else if (key == "sharpness") {
        v.sharpness = Clamp(f, 0.0f, 2.0f);
    } else if (key == "jitter") {
        v.jitter = i != 0;
    } else if (key == "reactive") {
        v.reactive = i != 0;
    } else if (key == "object_motion") {
        v.object_motion = i != 0;
    } else if (key == "reactive_scale") {
        v.reactive_scale = Clamp(f, 0.0f, 16.0f);
    } else if (key == "reactive_threshold") {
        v.reactive_threshold = Clamp(f, 0.0f, 1.0f);
    } else if (key == "reactive_max") {
        v.reactive_max = Clamp(f, 0.0f, 1.0f);
    } else if (key == "debug_view") {
        v.debug_view = std::clamp(i, 0, DebugViewCount - 1);
    } else if (key == "show_fps") {
        v.show_fps = i != 0;
    } else if (key == "display_mode") {
        v.display_mode = Gow3Graphics::ParseDisplayMode(value);
    } else if (key == "vsync") {
        v.vsync = i != 0;
    } else if (key == "fsr1") {
        v.fsr1 = i != 0;
    } else if (key == "rcas") {
        v.rcas = i != 0;
    } else if (key == "graphics_card") {
        v.graphics_card = value == "automatic" ? std::string() : value;
    } else if (key == "smaa") {
        v.smaa = i != 0;
    } else if (key == "anisotropy") {
        v.anisotropy = Gow3Graphics::ParseAnisotropy(i);
    } else if (key == "rcas_strength") {
        v.rcas_strength = Gow3Graphics::ParseRcasStrength(value.empty() ? -1 : i);
    } else if (key == "window_width") {
        v.requested_width = std::clamp(i, 0, 16384);
    } else if (key == "window_height") {
        v.requested_height = std::clamp(i, 0, 16384);
    } else if (key == "monitor") {
        v.monitor = std::max(i, 1);
    } else if (key == "show_fps_title") {
        v.show_fps_title = i != 0;
    } else if (key == "render_resolution") {
        v.render_resolution = Gow3Graphics::ParseResolution(value);
    } else if (key == "engine_fps") {
        v.engine_fps = Gow3Graphics::ParseEngineFps(i);
    } else if (key == "deferred_readback") {
        v.deferred_readback = i != 0;
    } else if (key == "stale_readback") {
        v.stale_readback = i != 0;
    } else if (key == "restart_unconfirmed") {
        v.restart_unconfirmed = i != 0;
    } else if (key == "fsr4_auto_exposure") {
        v.fsr4_auto_exposure = i != 0;
    } else if (key == "fsr4_invert_jitter") {
        v.fsr4_invert_jitter = i != 0;
    } else if (key == "live_resolution") {
        v.live_resolution = value == "auto" ? -1 : std::clamp(i, 0, 1);
    } else if (key == "output_res") {
        for (int r = 0; r < OutputCount; ++r) {
            if (value == std::to_string(OutputWidths[r]) + "x" + std::to_string(OutputHeights[r])) {
                v.output_res = r;
            }
        }
    }
}

} // namespace

Values& Get() {
    static Values values;
    return values;
}

void Load() {
    auto& v = Get();
    if (FILE* file = std::fopen(Path(), "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), file)) {
            std::string text{line};
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            const auto eq = text.find('=');
            if (text.empty() || text[0] == '#' || eq == std::string::npos) {
                continue;
            }
            Set(v, Trim(text.substr(0, eq)), Trim(text.substr(eq + 1)));
        }
        std::fclose(file);
        std::printf("Settings: %s\n", Path());
        if (v.smaa) {
            std::printf("GPU: anti-aliasing SMAA\n");
        }
        if (v.anisotropy) {
            std::printf("GPU: texture filtering %dx\n", v.anisotropy.load());
        }
    }
    // Environment overrides (scripts, A/B tests).
    if (const char* env = std::getenv("GOW3_UPSCALER")) {
        v.upscaler = UpscalerOff;
        for (int u = 0; u < UpscalerCount; ++u) {
            if (std::strcmp(env, UpscalerName(u)) == 0) v.upscaler = u;
        }
    }
    const std::pair<const char*, const char*> env_keys[] = {
        {"GOW3_FSR_SHARPNESS", "sharpness"},        {"GOW3_JITTER", "jitter"},
        {"GOW3_REACTIVE", "reactive"},              {"GOW3_REACTIVE_SCALE", "reactive_scale"},
        {"GOW3_REACTIVE_THRESHOLD", "reactive_threshold"}, {"GOW3_REACTIVE_MAX", "reactive_max"},
        {"GOW3_UPSCALE_PRESET", "preset"},            {"GOW3_OBJECT_MOTION", "object_motion"},
        {"GOW3_DEFERRED_READBACK", "deferred_readback"},
        {"GOW3_STALE_READBACK", "stale_readback"},     {"GOW3_FSR1", "fsr1"},
        {"GOW3_RCAS", "rcas"},
    };
    for (const auto& [env, key] : env_keys) {
        if (const char* value = std::getenv(env); value && value[0]) {
            Set(v, key, value);
        }
    }
    v.startup_preset = v.preset;
    v.startup_upscaler = v.upscaler;
    v.startup_object_motion = v.object_motion;
    v.startup_output_res = v.output_res;
    v.startup_live_resolution = v.live_resolution;
    v.startup_render_resolution = v.render_resolution;
    v.startup_engine_fps = v.engine_fps;
    v.startup_deferred_readback = v.deferred_readback;
    v.startup_stale_readback = v.stale_readback;
}

bool GraphicsNeedRestart() {
    const auto& v = Get();
    return v.render_resolution != v.startup_render_resolution ||
           v.engine_fps != v.startup_engine_fps ||
           v.deferred_readback != v.startup_deferred_readback ||
           v.stale_readback != v.startup_stale_readback;
}

void ConfigureUpscalerSupport(bool fsr4, bool fsr411) {
    auto& v = Get();
    v.fsr4_supported = fsr4;
    v.fsr411_supported = fsr4 && fsr411;
    const int requested = v.upscaler;
    if ((requested == UpscalerFsr4 && !v.fsr4_supported) ||
        (requested == UpscalerFsr411 && !v.fsr411_supported)) {
        v.fsr4_problem = "GPU does not support the selected FSR 4 shaders; using FSR 3.1";
        std::printf("Upscaler: %s unsupported on this GPU; falling back to FSR 3.1 before the first frame\n",
                    UpscalerName(requested));
        v.upscaler = UpscalerFsr3;
    }
}

void ConfigureDlssSupport(bool available, const char* problem) {
    auto& v = Get();
    v.dlss_supported = available;
    static std::string kept;
    kept = problem ? problem : "";
    v.dlss_problem = available || kept.empty() ? nullptr : kept.c_str();
    if (v.upscaler == UpscalerDlss && !available) {
        std::printf("Upscaler: DLSS unavailable (%s); falling back to FSR 3.1\n",
                    kept.empty() ? "gow3_dlss.dll or nvngx_dlss.dll missing" : kept.c_str());
        v.upscaler = UpscalerFsr3;
    }
}

bool FixedRenderSession() {
    const char* size = std::getenv("GOW3_RENDER_RES");
    return size && size[0];
}

int RenderPreset() {
    const auto& v = Get();
    return FixedRenderSession() ? v.startup_preset :
        v.upscaler == UpscalerTaa ? NativeAA : v.preset.load();
}

bool ResolutionNeedsRestart() {
    const auto& v = Get();
    // TAA needs the live path (native guest targets): run.sh selects it on restart.
    return FixedRenderSession() &&
        (v.preset != v.startup_preset || v.output_res != v.startup_output_res ||
         (v.upscaler == UpscalerOff) != (v.startup_upscaler == UpscalerOff) ||
         (v.upscaler == UpscalerTaa) != (v.startup_upscaler == UpscalerTaa));
}

void Save() {
    const auto& v = Get();
    std::string text;
    Appendf(text,
            "upscaler=%s\npreset=%d\nsharpen=%d\nsharpness=%.2f\njitter=%d\n"
            "reactive=%d\nobject_motion=%d\nreactive_scale=%.2f\nreactive_threshold=%.2f\nreactive_max=%.2f\n"
            "debug_view=%d\nshow_fps=%d\nfsr4_auto_exposure=%d\nfsr4_invert_jitter=%d\n",
            UpscalerName(v.upscaler), v.preset.load(), int(v.sharpen.load()),
            v.sharpness.load(), int(v.jitter.load()), int(v.reactive.load()),
            int(v.object_motion.load()),
            v.reactive_scale.load(), v.reactive_threshold.load(), v.reactive_max.load(),
            v.debug_view.load(), int(v.show_fps.load()),
            int(v.fsr4_auto_exposure.load()), int(v.fsr4_invert_jitter.load()));
    Appendf(text, "output_res=%dx%d\n", OutputWidths[v.output_res], OutputHeights[v.output_res]);
    Appendf(text,
            "overlay_fps=%d\noverlay_frametime=%d\noverlay_upscaler=%d\noverlay_gpu=%d\n"
            "overlay_cpu=%d\noverlay_ram=%d\noverlay_vram=%d\noverlay_scale=%d\noverlay_corner=%d\n"
            "overlay_opacity=%d\noverlay_layout=%d\nmenu_tab=%d\n",
            int(v.overlay_items >> 0 & 1), int(v.overlay_items >> 1 & 1), int(v.overlay_items >> 2 & 1),
            int(v.overlay_items >> 3 & 1), int(v.overlay_items >> 4 & 1), int(v.overlay_items >> 5 & 1),
            int(v.overlay_items >> 6 & 1), v.overlay_scale.load(), v.overlay_corner.load(),
            v.overlay_opacity.load(), v.overlay_layout.load(), v.menu_tab.load());
    Appendf(text,
            "display_mode=%s\nvsync=%d\nfps_limit=%d\nasync_shaders=%d\n"
            "fsr1=%d\nrcas=%d\nrcas_strength=%d\nframes_queued=%d\n"
            "render_resolution=%s\nengine_fps=%d\ndeferred_readback=%d\nstale_readback=%d\n"
            "restart_unconfirmed=%d\n",
            Gow3Graphics::DisplayModeKeys[std::clamp(v.display_mode.load(), 0, 2)].data(),
            int(v.vsync.load()), v.fps_limit.load(), int(v.async_shaders.load()),
            int(v.fsr1.load()), int(v.rcas.load()), v.rcas_strength.load(), v.frames_queued.load(),
            Gow3Graphics::Resolutions[std::clamp(v.render_resolution.load(), 0, 5)].data(),
            v.engine_fps.load(), int(v.deferred_readback.load()), int(v.stale_readback.load()),
            int(v.restart_unconfirmed.load()));
    // Read by run.sh at start.
    Appendf(text, "live_resolution=%s\n", v.live_resolution < 0 ? "auto"
                                             : v.live_resolution ? "1" : "0");
    WriteEntries(ParseEntries(text), FileHeader);
}

void SaveDisplayMode() {
    WriteEntries({{"display_mode", std::string(Gow3Graphics::DisplayModeKeys[std::clamp(Get().display_mode.load(), 0, 2)])}},
                 FileHeader);
}

float PresetScale(int preset) {
    static constexpr float scales[PresetCount] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    return scales[std::clamp(preset, 0, PresetCount - 1)];
}

const char* PresetName(int preset) {
    static constexpr const char* names[PresetCount] = {"Native AA", "Quality", "Balanced",
                                                       "Performance", "Ultra Performance"};
    return names[std::clamp(preset, 0, PresetCount - 1)];
}

const char* UpscalerName(int upscaler) {
    static constexpr const char* names[UpscalerCount] = {"off", "fsr3", "fsr4", "fsr411", "taa", "dlss"};
    return names[std::clamp(upscaler, 0, UpscalerCount - 1)];
}

} // namespace Gow3Settings
