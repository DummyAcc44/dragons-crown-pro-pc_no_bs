// Focused tests for the launcher's logic: game file validation, the settings file, the Play gate, the window
// placement and the Display options. They use synthetic files in a temporary folder; no window is opened.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "dcp_launcher.h"
#include "gow3_graphics.h"
#include "game_files.h"
#include "options.h"
#include "paths.h"
#include "placement.h"
#include "play_gate.h"
#include "runtime_log.h"
#include "settings.h"
#include "utf8.h"

namespace fs = std::filesystem;
using namespace dcp;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* what, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL line %d: %s\n", line, what);
    }
}
#define CHECK(condition) Check((condition), #condition, __LINE__)

std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void WriteAll(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// A param.sfo with the given string entries.
std::string MakeSfo(const std::vector<std::pair<std::string, std::string>>& items) {
    std::string keys, values;
    std::string entries;
    auto le16 = [](std::string& out, unsigned v) { out += char(v & 255); out += char(v >> 8 & 255); };
    auto le32 = [](std::string& out, unsigned v) {
        for (int i = 0; i < 4; ++i) out += char(v >> (8 * i) & 255);
    };
    for (const auto& [key, value] : items) {
        le16(entries, unsigned(keys.size()));
        le16(entries, 0x0204);
        le32(entries, unsigned(value.size() + 1));
        le32(entries, unsigned(value.size() + 1));
        le32(entries, unsigned(values.size()));
        keys += key + '\0';
        values += value + '\0';
    }
    while (keys.size() % 4) keys += '\0';
    std::string sfo(1, '\0');
    sfo += "PSF";
    le32(sfo, 0x101);
    le32(sfo, unsigned(20 + entries.size()));
    le32(sfo, unsigned(20 + entries.size() + keys.size()));
    le32(sfo, unsigned(items.size()));
    return sfo + entries + keys + values;
}

constexpr char kEboot[] = "synthetic eboot bytes";

struct Fixture {
    fs::path root;
    GamePaths paths;
    GameFingerprint fingerprint = kSupportedGame;
    std::string eboot_sha;

    explicit Fixture(const fs::path& base, const char* name) : root(base / name) {
        fs::remove_all(root);
        paths.app0 = root / "app0";
        paths.image_dir = root / "image";
        WriteAll(paths.app0 / "eboot.bin", kEboot);
        eboot_sha = Sha256Hex(paths.app0 / "eboot.bin");
        fingerprint.eboot_sha256 = eboot_sha;
        Rebuild();
    }

    // The complete, valid layout.
    void Rebuild() {
        WriteAll(paths.app0 / ".dcp_view.json", "{\n  \"version\": \"1.03\",\n  \"scope\": \"full\"\n}");
        WriteAll(paths.app0 / "sce_sys" / "param.sfo",
                 MakeSfo({{"TITLE", "Dragon's Crown Pro"},
                          {"TITLE_ID", std::string(kSupportedGame.title_id)},
                          {"CONTENT_ID", std::string(kSupportedGame.content_id)},
                          {"APP_VER", std::string(kSupportedGame.app_ver)},
                          {"VERSION", std::string(kSupportedGame.sfo_version)}}));
        for (const char* name : {"sce_discmap.plt", "DRACRO.CPK", "DRACRO_US.CPK", "sce_module/libc.prx",
                                 "sce_module/libSceFios2.prx", "sce_module/libSceNpToolkit2.prx"}) {
            WriteAll(paths.app0 / name, "data");
        }
        WriteAll(paths.image_dir / "boot.bin", "boot");
        WriteAll(paths.image_dir / "content.bin", "content");
        WriteAll(paths.image_dir / "image.json",
                 "{\n  \"title_id\": \"" + std::string(kSupportedGame.title_id) + "\",\n  \"eboot_sha256\": \"" + eboot_sha + "\"\n}");
    }

    GameFileState State() const { return CheckGameFiles(paths, fingerprint).state; }
};

void TestSha256AndJson(const fs::path& base) {
    WriteAll(base / "abc.txt", "abc");
    CHECK(Sha256Hex(base / "abc.txt") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    WriteAll(base / "empty.txt", "");
    CHECK(Sha256Hex(base / "empty.txt") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256Hex(base / "does-not-exist") .empty());

    const std::string json = "{\n  \"version\": \"1.03\",\n  \"scope\": \"full\",\n  \"layers\": [\"x\"]\n}";
    CHECK(JsonString(json, "version") == "1.03");
    CHECK(JsonString(json, "scope") == "full");
    CHECK(JsonString(json, "layers").empty());
    CHECK(JsonString(json, "missing").empty());
}

void TestGameFiles(const fs::path& base) {
    {
        Fixture f(base, "valid");
        const GameCheck check = CheckGameFiles(f.paths, f.fingerprint);
        CHECK(check.state == GameFileState::kValid);
        CHECK(check.eboot_sha256 == f.eboot_sha);
    }
    {
        Fixture f(base, "no-folder");
        fs::remove_all(f.paths.app0);
        CHECK(f.State() == GameFileState::kMissing);
    }
    {
        Fixture f(base, "no-marker");
        fs::remove(f.paths.app0 / ".dcp_view.json");
        CHECK(f.State() == GameFileState::kMissing);
    }
    {
        Fixture f(base, "unfinished-marker");
        WriteAll(f.paths.app0 / ".dcp_view.json", "{\n  \"description\": \"CUSA10454 1.03\"\n}");
        CHECK(f.State() == GameFileState::kMissing);
    }
    {
        Fixture f(base, "core-scope");
        WriteAll(f.paths.app0 / ".dcp_view.json", "{\"version\": \"1.03\", \"scope\": \"core\"}");
        CHECK(f.State() == GameFileState::kMissing);
    }
    {
        Fixture f(base, "other-version");
        WriteAll(f.paths.app0 / ".dcp_view.json", "{\"version\": \"1.04\", \"scope\": \"full\"}");
        CHECK(f.State() == GameFileState::kDifferentVersion);
    }
    {
        Fixture f(base, "other-title");
        WriteAll(f.paths.app0 / "sce_sys" / "param.sfo",
                 MakeSfo({{"TITLE_ID", "CUSA00000"},
                          {"CONTENT_ID", std::string(kSupportedGame.content_id)},
                          {"APP_VER", std::string(kSupportedGame.app_ver)},
                          {"VERSION", std::string(kSupportedGame.sfo_version)}}));
        CHECK(f.State() == GameFileState::kDifferentVersion);
    }
    {
        Fixture f(base, "other-sfo-version");
        WriteAll(f.paths.app0 / "sce_sys" / "param.sfo",
                 MakeSfo({{"TITLE_ID", std::string(kSupportedGame.title_id)},
                          {"CONTENT_ID", std::string(kSupportedGame.content_id)},
                          {"APP_VER", std::string(kSupportedGame.app_ver)},
                          {"VERSION", "01.00"}}));
        CHECK(f.State() == GameFileState::kDifferentVersion);
    }
    {
        Fixture f(base, "no-sfo");
        fs::remove(f.paths.app0 / "sce_sys" / "param.sfo");
        CHECK(f.State() == GameFileState::kMissing);
    }
    {
        Fixture f(base, "no-archive");
        fs::remove(f.paths.app0 / "DRACRO.CPK");
        CHECK(f.State() == GameFileState::kMissing);
    }
    {
        Fixture f(base, "empty-archive");
        WriteAll(f.paths.app0 / "DRACRO_US.CPK", "");
        CHECK(f.State() == GameFileState::kMissing);
    }
    {
        Fixture f(base, "other-eboot");
        WriteAll(f.paths.app0 / "eboot.bin", "a different build");
        CHECK(f.State() == GameFileState::kDifferentVersion);
    }
    {
        Fixture f(base, "no-image");
        fs::remove_all(f.paths.image_dir);
        CHECK(f.State() == GameFileState::kMissing);
    }
    {
        Fixture f(base, "stale-image");
        WriteAll(f.paths.image_dir / "image.json",
                 "{\"title_id\": \"" + std::string(kSupportedGame.title_id) + "\", \"eboot_sha256\": \"" + std::string(64, '0') + "\"}");
        CHECK(f.State() == GameFileState::kDifferentVersion);
    }
    {
        // The supported fingerprint rejects the synthetic eboot.
        Fixture f(base, "supported-fingerprint");
        CHECK(CheckGameFiles(f.paths).state == GameFileState::kDifferentVersion);
    }
}

// The C++ fingerprint must not drift from the profile the Python tools use.
void TestFingerprintMatchesProfile() {
    const std::string profile = ReadAll(fs::path(DCP_SOURCE_DIR) / "scripts" / "game_profiles.py");
    CHECK(!profile.empty());
    for (const std::string_view value : {kSupportedGame.title_id, kSupportedGame.content_id, kSupportedGame.eboot_sha256}) {
        CHECK(profile.find(std::string(value)) != std::string::npos);
    }
    CHECK(profile.find("'app_ver': '" + std::string(kSupportedGame.app_ver) + "'") != std::string::npos);
    CHECK(profile.find("'sfo_version': '" + std::string(kSupportedGame.sfo_version) + "'") != std::string::npos);
    CHECK(profile.find("'" + std::string(kSupportedGame.version) + "': {") != std::string::npos);
    CHECK(kSupportedGame.eboot_sha256.size() == 64);
}

void TestSettings(const fs::path& base) {
    const fs::path file = base / "settings" / "dcp.ini";
    fs::remove_all(file.parent_path());
    {
        LauncherSettings settings(file);
        CHECK(settings.GetBool(kShowLauncherKey, true));
        CHECK(settings.GetBool(kVSyncKey, true));
        CHECK(settings.GetInt(kWindowWidthKey, kDefaultWindowWidth) == kDefaultWindowWidth);
        CHECK(settings.Save()); // nothing pending: no file is needed
    }

    // A file the in-game menu wrote: "key=value" lines, keys the launcher does not manage, a comment.
    WriteAll(file,
             "# video core settings (in-game menu: Insert / R3+L2)\n"
             "upscaler=off\n"
             "show_fps=0\n"
             "vsync=1\n"
             "display_mode=fullscreen\n"
             "overlay_fps=1\n"
             "async_shaders=0\n");
    {
        LauncherSettings settings(file);
        CHECK(settings.GetBool(kVSyncKey, false));
        CHECK(settings.Get(kDisplayModeKey) == "borderless"); // exclusive fullscreen reads as Borderless Fullscreen

        settings.SetBool(kVSyncKey, false);
        settings.SetInt(kWindowWidthKey, 2560);
        settings.SetInt(kWindowHeightKey, 1440);
        settings.SetInt(kMonitorKey, 2);
        settings.SetBool(kShowFpsTitleKey, true);
        const std::string before_save = ReadAll(file);
        CHECK(before_save.find("vsync=1") != std::string::npos); // pending until Save
        CHECK(settings.Save());
    }
    {
        const std::string text = ReadAll(file);
        CHECK(text.find("upscaler=off") != std::string::npos);      // unrelated lines survive
        CHECK(text.find("overlay_fps=1") != std::string::npos);
        CHECK(text.find("async_shaders=0") != std::string::npos);
        CHECK(text.find("display_mode=fullscreen") != std::string::npos); // untouched: not changed in the launcher
        CHECK(text.find("# video core settings") != std::string::npos);
        CHECK(text.find("vsync = 0") != std::string::npos || text.find("vsync=0") != std::string::npos);
        LauncherSettings reread(file);
        CHECK(!reread.GetBool(kVSyncKey, true));
        CHECK(reread.GetInt(kWindowWidthKey, 0) == 2560);
        CHECK(reread.GetInt(kWindowHeightKey, 0) == 1440);
        CHECK(reread.GetInt(kMonitorKey, 0) == 2);
        CHECK(reread.GetBool(kShowFpsTitleKey, false));
        CHECK(reread.Get(kDisplayModeKey) == "borderless");
    }

    // Booleans are 1/0: the video core reads them with atoi.
    {
        LauncherSettings settings(file);
        settings.SetBool(kSharpenKey, false);
        CHECK(settings.Get(kSharpenKey) == "0");
        settings.SetBool(kSharpenKey, true);
        CHECK(settings.Get(kSharpenKey) == "1");
    }

    // SaveNow (the window placement) leaves other pending edits pending.
    {
        LauncherSettings settings(file);
        settings.SetBool(kUpscaleKey, true);
        WindowPlacement placement;
        placement.normal = {100, 80, 1380, 800};
        placement.maximized = true;
        CHECK(settings.SaveNow(PlacementValues(placement)));
        CHECK(ReadAll(file).find("fsr1") == std::string::npos);
        CHECK(settings.GetBool(kUpscaleKey, false)); // still pending in memory
        LauncherSettings reread(file);
        const WindowPlacement stored = ReadPlacement(reread);
        CHECK(stored == placement);
        CHECK(!reread.GetBool(kUpscaleKey, false));
    }

    // Reset restores the defaults but keeps the launcher's own window placement.
    {
        LauncherSettings settings(file);
        settings.SetBool(kShowLauncherKey, false);
        settings.SetInt(kWindowWidthKey, 3840);
        settings.ResetToDefaults();
        CHECK(settings.GetBool(kShowLauncherKey, false));
        CHECK(settings.GetInt(kWindowWidthKey, 0) == kDefaultWindowWidth);
        CHECK(settings.GetInt(kLauncherWidthKey, 0) == 1280);
        CHECK(settings.Save());
        LauncherSettings reread(file);
        CHECK(reread.GetBool(kShowLauncherKey, false));
        CHECK(reread.GetInt(kLauncherWidthKey, 0) == 1280);
        CHECK(ReadAll(file).find("overlay_fps=1") != std::string::npos);
    }

    // A file that cannot be written reports it and keeps the edits pending.
    {
        LauncherSettings settings(base / "settings" / "dcp.ini" / "not-a-folder" / "dcp.ini");
        settings.SetBool(kShowLauncherKey, false);
        CHECK(!settings.Save());
        CHECK(!settings.GetBool(kShowLauncherKey, true));
    }
}

void TestPaths() {
    const PathRequest none;
    const fs::path version = "v1.03";

    const fs::path packaged = "X:/DragonsCrownPro";
    Paths p = ResolvePaths(none, packaged);
    CHECK(p.app0 == packaged / "game" / "prepared" / version);
    CHECK(p.image_dir == packaged / "game" / "image" / version);
    CHECK(p.boot == p.image_dir / "boot.bin");
    CHECK(p.content_profile == p.image_dir / "content.bin");
    CHECK(p.user_dir == packaged / "user");
    CHECK(p.logs_dir == packaged / "logs");

    p = ResolvePaths(none, "S:/repo/build/cmake/clang64/bin");
    CHECK(p.app0 == fs::path("S:/repo/game/prepared") / version);
    CHECK(p.image_dir == fs::path("S:/repo/build/image") / version);
    CHECK(p.user_dir == fs::path("S:/repo/user"));
    CHECK(p.logs_dir == fs::path("S:/repo/build/logs"));

    p = ResolvePaths(none, "X:/Apps/bin"); // a bin folder outside a build tree is just a packaged folder
    CHECK(p.app0 == fs::path("X:/Apps/bin/game/prepared") / version);
    p = ResolvePaths(none, "S:/repo/build/cmake/clang64/other");
    CHECK(p.user_dir == fs::path("S:/repo/build/cmake/clang64/other/user"));

    const PathRequest all{"D:/img/boot.bin", "D:/content/content.bin", "D:/game", "D:/user", "D:/logs"};
    p = ResolvePaths(all, packaged);
    CHECK(p.boot == fs::path("D:/img/boot.bin") && p.image_dir == fs::path("D:/img"));
    CHECK(p.content_profile == fs::path("D:/content/content.bin"));
    CHECK(p.app0 == fs::path("D:/game") && p.user_dir == fs::path("D:/user") && p.logs_dir == fs::path("D:/logs"));

    PathRequest boot_only;
    boot_only.boot = "D:/img/boot.bin";
    p = ResolvePaths(boot_only, packaged);
    CHECK(p.content_profile == fs::path("D:/img/content.bin")); // next to the boot file
    CHECK(p.app0 == packaged / "game" / "prepared" / version);

    PathRequest bare;
    bare.boot = "boot.bin";
    CHECK(ResolvePaths(bare, packaged).image_dir == fs::path("."));

    PathRequest user_only;
    user_only.user_dir = "D:/user";
    p = ResolvePaths(user_only, packaged);
    CHECK(p.user_dir == fs::path("D:/user") && p.logs_dir == packaged / "logs");

    // The C entry point finds this test executable in the build tree: the repository's folders.
    DcpPaths found;
    const DcpPathRequest nothing{};
    CHECK(dcp_resolve_paths(&nothing, &found) == 1);
    const fs::path repository = DCP_SOURCE_DIR;
    CHECK(fs::weakly_canonical(PathFromUtf8(found.user_dir)) == fs::weakly_canonical(repository / "user"));
    CHECK(fs::weakly_canonical(PathFromUtf8(found.app0)) == fs::weakly_canonical(repository / "game" / "prepared" / version));
    CHECK(fs::weakly_canonical(PathFromUtf8(found.boot)) == fs::weakly_canonical(repository / "build" / "image" / version / "boot.bin"));
    CHECK(fs::weakly_canonical(PathFromUtf8(found.logs_dir)) == fs::weakly_canonical(repository / "build" / "logs"));
    const DcpPathRequest explicit_user{nullptr, nullptr, nullptr, "D:/elsewhere", nullptr};
    CHECK(dcp_resolve_paths(&explicit_user, &found) == 1 && std::string(found.user_dir) == "D:/elsewhere");
}

// Creates a log the way the loader does and writes some text into it; the path, or empty on failure.
std::string CreateLog(const fs::path& dir, const char* file = nullptr, const std::string& text = {}) {
    char path[4096];
    void* handle = nullptr;
    const std::string folder = dir.string();
    if (!runtime_log_create(folder.c_str(), file, path, sizeof(path), &handle)) {
        return {};
    }
    DWORD written = 0;
    WriteFile(handle, text.data(), DWORD(text.size()), &written, nullptr);
    CloseHandle(handle);
    return path;
}

void TestLogNumbering(const fs::path& base) {
    const fs::path dir = base / "numbered" / "logs";
    const std::string first = CreateLog(dir, nullptr, "one");
    CHECK(fs::path(first).filename() == "dragons_crown_pro_001.log");
    const std::string second = CreateLog(dir, nullptr, "two");
    CHECK(fs::path(second).filename() == "dragons_crown_pro_002.log");
    CHECK(ReadAll(first) == "one" && ReadAll(second) == "two"); // an earlier log is never overwritten

    const fs::path busy = base / "busy";
    WriteAll(busy / "dragons_crown_pro_007.log", "seven");
    WriteAll(busy / "dragons_crown_pro_abc.log", "x");
    WriteAll(busy / "dragons_crown_pro_5.log.bak", "x");
    WriteAll(busy / "other_009.log", "x");
    const std::string next = CreateLog(busy, nullptr, "eight");
    CHECK(fs::path(next).filename() == "dragons_crown_pro_008.log");
    CHECK(ReadAll(busy / "dragons_crown_pro_007.log") == "seven");
    CHECK(fs::path(CreateLog(busy)).filename() == "dragons_crown_pro_009.log");
    WriteAll(busy / "dragons_crown_pro_1000.log", "x");
    CHECK(fs::path(CreateLog(busy)).filename() == "dragons_crown_pro_1001.log");

    const fs::path explicit_file = base / "explicit" / "deeper" / "my.log";
    CHECK(CreateLog(base / "ignored", explicit_file.string().c_str(), "mine") == explicit_file.string());
    CHECK(ReadAll(explicit_file) == "mine" && !fs::exists(base / "ignored"));
}

void TestDefaultLogFolders(const fs::path& base) {
    const PathRequest none;
    const fs::path packaged = base / "packaged";
    fs::create_directories(packaged);
    const std::string packaged_log = CreateLog(ResolvePaths(none, packaged).logs_dir);
    CHECK(fs::path(packaged_log) == packaged / "logs" / "dragons_crown_pro_001.log");
    CHECK(fs::path(CreateLog(ResolvePaths(none, packaged).logs_dir)) == packaged / "logs" / "dragons_crown_pro_002.log");

    const fs::path bin = base / "repo" / "build" / "cmake" / "clang64" / "bin";
    fs::create_directories(bin);
    CHECK(fs::path(CreateLog(ResolvePaths(none, bin).logs_dir)) == base / "repo" / "build" / "logs" / "dragons_crown_pro_001.log");

    PathRequest asked;
    asked.logs_dir = (base / "custom").string();
    CHECK(fs::path(CreateLog(ResolvePaths(asked, packaged).logs_dir)) == base / "custom" / "dragons_crown_pro_001.log");
}

// Last: it redirects this process's stdout and stderr.
void TestLogRedirect(const fs::path& base) {
    char path[4096];
    CHECK(runtime_log_start((base / "tee").string().c_str(), nullptr, path, sizeof(path)) == 1);
    std::printf("stdout marker\n");
    std::fflush(stdout);
    std::fprintf(stderr, "stderr marker\n");
    runtime_log_flush();
    const std::string text = ReadAll(path);
    CHECK(fs::path(path).filename() == "dragons_crown_pro_001.log");
    CHECK(text.find("stdout marker\nstderr marker\n") != std::string::npos);
}

// The safe video core defaults live in the loader, not in the development scripts.
void TestRuntimeDefaultsLiveInTheLoader() {
    const std::string loader = ReadAll(fs::path(DCP_SOURCE_DIR) / "src" / "loader.c");
    for (const char* line : {"default_env(\"GOW3_UPSCALER\", \"none\")", "default_env(\"GOW3_READBACK_LINEAR\", \"1\")",
                             "default_env(\"GOW3_VBLANK_HZ\", \"60\")"}) {
        CHECK(loader.find(line) != std::string::npos);
    }
    CHECK(ReadAll(fs::path(DCP_SOURCE_DIR) / "scripts" / "run_game.py").find("GOW3_") == std::string::npos);
    CHECK(ReadAll(fs::path(DCP_SOURCE_DIR) / "scripts" / "game_profiles.py").find("GOW3_") == std::string::npos);
}

void TestCorrectnessStaysFixed() {
    const fs::path source(DCP_SOURCE_DIR);
    const std::string settings = ReadAll(source / "gpu" / "shim" / "gow3_settings.cpp");
    CHECK(settings.find("key == \"async_shaders\"") == std::string::npos);
    CHECK(settings.find("GOW3_ASYNC_SHADERS") == std::string::npos);
    CHECK(settings.find("key == \"fps_limit\"") == std::string::npos);
    CHECK(settings.find("key == \"frames_queued\"") == std::string::npos);
    CHECK(settings.find("GOW3_FRAMES_AHEAD") == std::string::npos);
    CHECK(ReadAll(source / "gpu" / "shim" / "gow3_settings.h").find("fps_limit{0}") != std::string::npos);
    CHECK(ReadAll(source / "gpu" / "shim" / "gow3_settings.h").find("frames_queued{1}") != std::string::npos);
    const std::string emulator = ReadAll(source / "gpu" / "shim" / "core" / "emulator_settings.h");
    CHECK(emulator.find("u32 GetFrameLimit() { return 0; }") != std::string::npos);
    CHECK(emulator.find("GOW3_FPS_LIMIT") == std::string::npos);
    CHECK(emulator.find("u32 GetVblankFrequency() { return 60; }") != std::string::npos);
    CHECK(ReadAll(source / "gpu" / "shadps4" / "core" / "libraries" / "videoout" / "driver.cpp")
              .find("getenv(\"GOW3_FPS_LIMIT\")") == std::string::npos);
    const std::string menu = ReadAll(source / "gpu" / "shim" / "gow3_overlay.cpp");
    CHECK(menu.find("Frame rate limit") == std::string::npos);
    CHECK(menu.find("Frames queued") == std::string::npos);
    CHECK(ReadAll(source / "gpu" / "shim" / "gow3_settings.h").find("async_shaders{false}") != std::string::npos);
    CHECK(ReadAll(source / "gpu" / "shim" / "gow3_overlay.cpp").find("Compile new shaders") == std::string::npos);
    const std::string launcher = ReadAll(source / "launcher" / "launcher.cpp");
    for (const char* hidden : {"async", "workers", "frame_limit", "fps_limit", "engine_fps", "frames_queued"}) {
        CHECK(launcher.find(hidden) == std::string::npos);
    }
}

bool WrittenValue(const std::string& text, const std::string& key, const std::string& value) {
    return text.find(key + "=" + value) != std::string::npos || text.find(key + " = " + value) != std::string::npos;
}

void TestRenderResolution(const fs::path& base) {
    const fs::path source(DCP_SOURCE_DIR);
    const fs::path file = base / "render" / "dcp.ini";
    fs::remove_all(file.parent_path());
    CHECK(dcp_render_resolution(file.string().c_str()) == 0); // no file: native
    CHECK(dcp_render_resolution(nullptr) == 0);

    struct Expected { const char* key; const char* name; int width, height, pro; };
    const Expected expected[] = {{"native", "Native (1080p)", 1920, 1080, 0}, {"480p", "480p", 854, 480, 0},
                                 {"720p", "720p", 1280, 720, 0},              {"1440p", "1440p", 2560, 1440, 1},
                                 {"1800p", "1800p", 3200, 1800, 1},           {"4K", "4K", 3840, 2160, 1}};
    for (int i = 0; i < DCP_RESOLUTION_COUNT; ++i) {
        CHECK(std::string(dcp_resolutions[i].key) == expected[i].key);
        CHECK(std::string(dcp_resolutions[i].name) == expected[i].name);
        CHECK(dcp_resolutions[i].width == expected[i].width && dcp_resolutions[i].height == expected[i].height);
        CHECK(dcp_resolutions[i].pro_memory == expected[i].pro);
        CHECK(ResolutionLabel(dcp_resolutions[i]).find(expected[i].name) == 0);
        {
            LauncherSettings settings(file);
            settings.Set(kRenderResolutionKey, expected[i].key);
            CHECK(settings.Save());
        }
        CHECK(WrittenValue(ReadAll(file), "render_resolution", expected[i].key));
        CHECK(dcp_render_resolution(file.string().c_str()) == i);
    }
    CHECK(ResolutionLabel(dcp_resolutions[0]) == "Native (1080p) \xE2\x80\x94 1920 \xC3\x97 1080");
    {
        LauncherSettings settings(file);
        settings.ResetToDefaults();
        CHECK(settings.Save());
    }
    CHECK(WrittenValue(ReadAll(file), "render_resolution", "native"));
    CHECK(dcp_render_resolution(file.string().c_str()) == 0);
    for (const char* unlisted : {"720P", "900p", "8K", ""}) {
        WriteAll(file, std::string("render_resolution=") + unlisted + "\n");
        CHECK(dcp_render_resolution(file.string().c_str()) == 0);
    }

    bool managed = false;
    for (const ManagedSetting& setting : ManagedSettings()) {
        managed |= setting.key == "render_resolution" && setting.default_value == "native";
    }
    CHECK(managed);

    size_t count = 0;
    const DcpResolutionSite* sites = dcp_resolution_sites(&count);
    CHECK(count == 23);
    uint64_t image_size = 0;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t end = sites[i].offset + sites[i].opcode_length + 4;
        image_size = std::max<uint64_t>(image_size, end);
        const bool width_value = sites[i].value == 1920 || sites[i].value == 3840;
        const bool height_value = sites[i].value == 1080 || sites[i].value == 2160;
        CHECK(sites[i].height ? height_value : width_value); // each site holds the right dimension
        for (size_t j = i + 1; j < count; ++j) {
            const uint32_t other_end = sites[j].offset + sites[j].opcode_length + 4;
            CHECK(end <= sites[j].offset || other_end <= sites[i].offset); // no overlap
        }
    }
    const auto make_image = [&] {
        std::vector<unsigned char> image(image_size + 16, 0x90);
        for (size_t i = 0; i < count; ++i) {
            std::memcpy(&image[sites[i].offset], sites[i].opcode, sites[i].opcode_length);
            std::memcpy(&image[sites[i].offset + sites[i].opcode_length], &sites[i].value, 4);
        }
        return image;
    };
    char error[160];
    for (const DcpResolution& resolution : dcp_resolutions) {
        std::vector<unsigned char> image = make_image();
        CHECK(dcp_apply_resolution(image.data(), image.size(), &resolution, error, sizeof(error)));
        for (size_t i = 0; i < count; ++i) {
            uint32_t value = 0;
            std::memcpy(&value, &image[sites[i].offset + sites[i].opcode_length], 4);
            CHECK(value == static_cast<uint32_t>(sites[i].height ? resolution.height : resolution.width));
            CHECK(std::memcmp(&image[sites[i].offset], sites[i].opcode, sites[i].opcode_length) == 0);
        }
    }
    {
        std::vector<unsigned char> image = make_image();
        image[sites[7].offset + sites[7].opcode_length] ^= 1; // one site no longer holds the original value
        const std::vector<unsigned char> before = image;
        CHECK(!dcp_apply_resolution(image.data(), image.size(), &dcp_resolutions[3], error, sizeof(error)));
        CHECK(image == before); // nothing written
        CHECK(std::string(error).find("unexpected instruction") != std::string::npos);
        CHECK(!dcp_apply_resolution(image.data(), 0x1000, &dcp_resolutions[3], error, sizeof(error)));
    }

    const std::string launcher = ReadAll(source / "launcher" / "launcher.cpp");
    CHECK(launcher.find("PageGraphics(trg::Ui& ui)") != std::string::npos);
    CHECK(std::string(kRenderResolutionKey) != kWindowWidthKey && std::string(kRenderResolutionKey) != kWindowHeightKey);
    CHECK(ReadAll(source / "launcher" / "resolution.c").find("kWindow") == std::string::npos);
    CHECK(ReadAll(source / "src" / "runtime_dcp.c").find("is_neo_mode(void) { return neo_mode; }") != std::string::npos);
}

void TestGraphicsQuality(const fs::path& base) {
    const fs::path source(DCP_SOURCE_DIR);
    const fs::path file = base / "quality" / "dcp.ini";
    fs::remove_all(file.parent_path());
    bool smaa_default = false, aniso_default = false;
    for (const ManagedSetting& setting : ManagedSettings()) {
        smaa_default |= setting.key == "smaa" && setting.default_value == "0";
        aniso_default |= setting.key == "anisotropy" && setting.default_value == "0";
    }
    CHECK(smaa_default && aniso_default);
    for (const char* level : {"2", "4", "8", "16"}) {
        {
            LauncherSettings settings(file);
            settings.SetBool(kAntiAliasingKey, true);
            settings.Set(kAnisotropyKey, level);
            CHECK(settings.Save());
        }
        const std::string text = ReadAll(file);
        CHECK(WrittenValue(text, "smaa", "1"));
        CHECK(WrittenValue(text, "anisotropy", level));
        LauncherSettings reread(file);
        CHECK(reread.GetBool(kAntiAliasingKey, false));
        CHECK(reread.Get(kAnisotropyKey, "0") == level);
    }
    {
        LauncherSettings settings(file);
        settings.ResetToDefaults();
        CHECK(settings.Save());
    }
    CHECK(WrittenValue(ReadAll(file), "smaa", "0"));
    CHECK(WrittenValue(ReadAll(file), "anisotropy", "0"));

    const std::string graphics = ReadAll(source / "gpu" / "shim" / "gow3_graphics.h");
    CHECK(graphics.find("Anisotropy = {0, 2, 4, 8, 16}") != std::string::npos);
    const std::string loader = ReadAll(source / "gpu" / "shim" / "gow3_settings.cpp");
    CHECK(loader.find("key == \"smaa\"") != std::string::npos);
    CHECK(loader.find("ParseAnisotropy(i)") != std::string::npos);
    const std::string header = ReadAll(source / "gpu" / "shim" / "gow3_settings.h");
    CHECK(header.find("smaa{false}") != std::string::npos);
    CHECK(header.find("anisotropy{0}") != std::string::npos);
    const std::string launcher = ReadAll(source / "launcher" / "launcher.cpp");
    const size_t resolution = launcher.find("\"Render resolution\"");
    const size_t anti_aliasing = launcher.find("\"Anti-aliasing\"");
    const size_t filtering = launcher.find("\"Texture filtering\"");
    CHECK(resolution != std::string::npos && resolution < anti_aliasing && anti_aliasing < filtering);
    CHECK(launcher.find("MSAA") == std::string::npos && launcher.find("Multisampling") == std::string::npos);
}

void TestGraphicsCard(const fs::path& base) {
    const fs::path source(DCP_SOURCE_DIR);
    const fs::path file = base / "gpu" / "dcp.ini";
    fs::remove_all(file.parent_path());
    const std::vector<std::string> names = {"NVIDIA GeForce RTX 4070", "", "Intel(R) UHD Graphics 730"};
    CHECK(Gow3Graphics::FindGpuByName(names, "") == -1);
    CHECK(Gow3Graphics::FindGpuByName(names, "automatic") == -1);
    CHECK(Gow3Graphics::FindGpuByName(names, "NVIDIA GeForce RTX 4070") == 0);
    CHECK(Gow3Graphics::FindGpuByName(names, "Intel(R) UHD Graphics 730") == 2);
    CHECK(Gow3Graphics::FindGpuByName(names, "Intel(R) UHD Graphics 7") == -1); // exact names only
    CHECK(Gow3Graphics::FindGpuByName(names, "AMD Radeon RX 7800 XT") == -1);   // removed card: Automatic
    CHECK(Gow3Graphics::FindGpuByName({"", "Intel(R) UHD Graphics 730"}, "") == -1);

    bool managed = false;
    for (const ManagedSetting& setting : ManagedSettings()) {
        managed |= setting.key == "graphics_card" && setting.default_value == "automatic";
    }
    CHECK(managed);
    {
        LauncherSettings settings(file);
        CHECK(settings.Get(kGraphicsCardKey, kAutomaticGpu) == "automatic");
        settings.Set(kGraphicsCardKey, "Intel(R) UHD Graphics 730");
        CHECK(settings.Save());
    }
    CHECK(WrittenValue(ReadAll(file), "graphics_card", "Intel(R) UHD Graphics 730"));
    CHECK(LauncherSettings(file).Get(kGraphicsCardKey, kAutomaticGpu) == "Intel(R) UHD Graphics 730");
    {
        LauncherSettings settings(file);
        settings.ResetToDefaults();
        CHECK(settings.Save());
    }
    CHECK(WrittenValue(ReadAll(file), "graphics_card", "automatic"));

    const std::string instance = ReadAll(source / "gpu" / "shadps4" / "video_core" / "renderer_vulkan" / "vk_instance.cpp");
    CHECK(instance.find("FindGpuByName(names, requested)") != std::string::npos);
    CHECK(instance.find("IsDeviceUsable(physical) ? ") != std::string::npos);
    CHECK(instance.find("const bool left_is_discrete") != std::string::npos); // the Automatic ranking is intact
    CHECK(ReadAll(source / "gpu" / "shim" / "gow3_settings.cpp").find("key == \"graphics_card\"") != std::string::npos);
    const std::string launcher = ReadAll(source / "launcher" / "launcher.cpp");
    const size_t monitor = launcher.find("ui.Row(\"Monitor\"");
    const size_t card = launcher.find("ui.Row(\"Graphics card\"");
    const size_t vsync = launcher.find("ui.Toggle(\"VSync\"");
    CHECK(monitor != std::string::npos && monitor < card && card < vsync);
}

void TestLauncherWanted() {
    CHECK(LauncherWanted(false, false, false, false));  // not ready: always
    CHECK(LauncherWanted(false, false, false, true));   // even when skipped once
    CHECK(LauncherWanted(true, true, false, false));    // ready, setting on
    CHECK(!LauncherWanted(true, false, false, false));  // ready, setting off
    CHECK(LauncherWanted(true, false, true, false));    // Shift forces it
    CHECK(!LauncherWanted(true, true, true, true));     // skipped once
}

void TestPlayGate() {
    {
        PlayGate gate;
        int saves = 0;
        const auto save = [&] { ++saves; return true; };
        CHECK(gate.Request(false, true, save) == PlayRequest::kStarting);
        CHECK(gate.starting());
        CHECK(gate.Request(false, true, save) == PlayRequest::kBusy); // no double trigger
        CHECK(saves == 1);
    }
    {
        PlayGate gate;
        int saves = 0;
        CHECK(gate.Request(false, false, [&] { ++saves; return true; }) == PlayRequest::kFilesNotReady);
        CHECK(saves == 0 && !gate.starting());
        CHECK(gate.Request(true, true, [&] { ++saves; return true; }) == PlayRequest::kBusy);
        CHECK(saves == 0);
    }
    {
        PlayGate gate;
        CHECK(gate.Request(false, true, [] { return false; }) == PlayRequest::kSaveFailed);
        CHECK(!gate.starting());
    }
}

void TestPlacement() {
    const std::vector<Rect> areas = {{0, 0, 1920, 1080}, {1920, 0, 3840, 1080}};
    WindowPlacement on_second{{2000, 100, 3280, 820}};
    WindowPlacement gone{{5000, 100, 6280, 820}};
    WindowPlacement tiny{{10, 10, 100, 100}};
    CHECK(PlacementIsVisible(on_second, areas));
    CHECK(!PlacementIsVisible(gone, areas));
    CHECK(!PlacementIsVisible(tiny, areas));
    CHECK(!PlacementIsVisible(on_second, std::vector<Rect>{{0, 0, 1920, 1080}})); // that display was unplugged

    PlacementTracker tracker;
    const WindowState first{{100, 100, 1380, 820}};
    tracker.Observe(first, false, 0.0);
    CHECK(!tracker.TakeSettled(10.0)); // the starting window is not news
    const WindowState moved{{200, 150, 1480, 870}};
    tracker.Observe(moved, false, 1.0);
    CHECK(!tracker.TakeSettled(1.2)); // still settling
    const auto settled = tracker.TakeSettled(1.6);
    CHECK(settled && settled->normal == moved.rect);
    CHECK(!tracker.TakeSettled(5.0)); // saved once

    WindowState maximized{{0, 0, 1920, 1040}, true, false};
    tracker.Observe(maximized, false, 6.0);
    const auto kept = tracker.TakeSettled(7.0);
    CHECK(kept && kept->maximized && kept->normal == moved.rect); // the restored rectangle is kept
    tracker.Observe(maximized, true, 8.0);
    const auto fullscreen = tracker.TakeSettled(9.0);
    CHECK(fullscreen && fullscreen->fullscreen && fullscreen->normal == moved.rect);
}

void TestOptions() {
    Display display;
    display.usable = {0, 0, 1920, 1040};
    display.bounds = {0, 0, 1920, 1080};
    display.scale = 1.0f;
    CHECK(WindowSizeFits({1280, 720}, display));
    CHECK(WindowSizeFits({1600, 900}, display));
    CHECK(!WindowSizeFits({1920, 1080}, display)); // the title bar leaves no room on a 1080p desktop
    CHECK(!WindowSizeFits({2560, 1440}, display));
    const WindowSize fitted = FitGameWindow({1920, 1080}, display);
    CHECK(fitted.width <= 1920 && fitted.height <= 1040 - 40);
    CHECK(fitted.width * 9 / 16 >= fitted.height - 1 && fitted.width * 9 / 16 <= fitted.height + 1); // shape kept

    Display large;
    large.usable = {0, 0, 3840, 2100};
    large.bounds = {0, 0, 3840, 2160};
    large.scale = 2.25f;
    CHECK(WindowSizeFits({3840, 2160}, large) == false);
    CHECK(WindowSizeFits({2560, 1440}, large));
    CHECK(FitGameWindow({1920, 1080}, large) == WindowSize({1920, 1080}));

    CHECK(WindowSizeLabel({1280, 720}) == "1280 \xC3\x97 720");
    CHECK(WindowSizeLabel(kGamePictureSize).find("1920 \xC3\x97 1080") == 0);

    std::vector<Display> displays(2);
    displays[0].bounds = {0, 0, 3840, 2160};
    displays[0].primary = true;
    displays[1].bounds = {3840, 0, 1920, 1080};
    CHECK(MonitorLabel(1, displays).find("Display 1 (primary)") == 0);
    CHECK(MonitorLabel(2, displays).find("Display 2") == 0);
    CHECK(MonitorLabel(3, displays).find("not connected") != std::string::npos);
    CHECK(DisplayForSetting(displays, 2) == &displays[1]);
    CHECK(DisplayForSetting(displays, 0) == &displays[0]);
    CHECK(DisplayForSetting(displays, 9) == &displays[0]);
    CHECK(DisplayForSetting({}, 1) == nullptr);

    int count = 0;
    for (const PageId page : kPageOrder) {
        CHECK(std::string(PageName(page)).size() > 0);
        ++count;
    }
    CHECK(count == 6);
    CHECK(std::string(PageName(kPageOrder[0])) == "Play");
    CHECK(std::string(PageName(kPageOrder[2])) == "Graphics");
    CHECK(std::string(PageName(kPageOrder[5])) == "About");
    CHECK(!KeyboardBindings().empty());
}

} // namespace

int main(int argc, char** argv) {
    // dcp-launcher-tests --check-game <app0> <image dir>: the readiness check on real folders, read-only.
    if (argc == 4 && std::string(argv[1]) == "--check-game") {
        const GameCheck check = CheckGameFiles({PathFromUtf8(argv[2]), PathFromUtf8(argv[3])});
        std::printf("%s%s%s\neboot.bin SHA-256 %s\n", StateName(check.state), check.detail.empty() ? "" : ": ",
                    check.detail.c_str(), check.eboot_sha256.c_str());
        return check.state == GameFileState::kValid ? 0 : 1;
    }

    const fs::path base = fs::temp_directory_path() / ("dcp_launcher_tests_" + std::to_string(getpid()));
    fs::remove_all(base);
    fs::create_directories(base);

    TestSha256AndJson(base);
    TestGameFiles(base);
    TestFingerprintMatchesProfile();
    TestSettings(base);
    TestPaths();
    TestLogNumbering(base);
    TestDefaultLogFolders(base);
    TestRuntimeDefaultsLiveInTheLoader();
    TestCorrectnessStaysFixed();
    TestRenderResolution(base);
    TestGraphicsQuality(base);
    TestGraphicsCard(base);
    TestLauncherWanted();
    TestPlayGate();
    TestPlacement();
    TestOptions();

    std::error_code ec;
    fs::remove_all(base, ec);
    const fs::path redirected = fs::temp_directory_path() / "dcp_launcher_tests_log"; // kept: the log stays open
    fs::remove_all(redirected, ec);
    TestLogRedirect(redirected);
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    runtime_log_flush();
    return g_failures ? 1 : 0;
}
