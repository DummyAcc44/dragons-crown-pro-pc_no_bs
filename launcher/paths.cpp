#include "paths.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstring>

#include "dcp_launcher.h"
#include "game_profile.h"
#include "utf8.h"

namespace fs = std::filesystem;

namespace dcp {
namespace {

struct Defaults {
    fs::path app0;
    fs::path image_dir;
    fs::path user_dir;
    fs::path logs_dir;
};

Defaults DefaultsFor(const fs::path& exe_dir) {
    const fs::path version = fs::path("v" + std::string(kSupportedGame.version));
    const fs::path build = exe_dir.parent_path().parent_path().parent_path();
    const bool in_build_tree = exe_dir.filename() == "bin" && exe_dir.parent_path().parent_path().filename() == "cmake" &&
                               build.filename() == "build";
    if (in_build_tree) {
        const fs::path root = build.parent_path();
        return {root / "game" / "prepared" / version, build / "image" / version, root / "user", build / "logs"};
    }
    return {exe_dir / "game" / "prepared" / version, exe_dir / "game" / "image" / version, exe_dir / "user",
            exe_dir / "logs"};
}

fs::path OrDefault(const std::string& given, const fs::path& fallback) {
    return given.empty() ? fallback : PathFromUtf8(given);
}

bool Copy(char (&out)[4096], const fs::path& path) {
    const std::string text = Utf8(path);
    if (text.size() >= sizeof(out)) {
        return false;
    }
    std::memcpy(out, text.c_str(), text.size() + 1);
    return true;
}

} // namespace

Paths ResolvePaths(const PathRequest& request, const fs::path& exe_dir) {
    const Defaults defaults = DefaultsFor(exe_dir);
    Paths paths;
    if (request.boot.empty()) {
        paths.image_dir = defaults.image_dir;
        paths.boot = paths.image_dir / "boot.bin";
    } else {
        paths.boot = PathFromUtf8(request.boot);
        paths.image_dir = paths.boot.has_parent_path() ? paths.boot.parent_path() : fs::path(".");
    }
    paths.content_profile = request.content_profile.empty() ? paths.image_dir / "content.bin"
                                                            : PathFromUtf8(request.content_profile);
    paths.app0 = OrDefault(request.app0, defaults.app0);
    paths.user_dir = OrDefault(request.user_dir, defaults.user_dir);
    paths.logs_dir = OrDefault(request.logs_dir, defaults.logs_dir);
    return paths;
}

} // namespace dcp

extern "C" int dcp_resolve_paths(const DcpPathRequest* request, DcpPaths* out) {
    std::wstring module(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0 || length >= module.size()) {
        return 0;
    }
    const auto text = [](const char* value) { return std::string(value ? value : ""); };
    const dcp::PathRequest asked{text(request->boot), text(request->content_profile), text(request->app0),
                                 text(request->user_dir), text(request->logs_dir)};
    const dcp::Paths paths = dcp::ResolvePaths(asked, fs::path(module.substr(0, length)).parent_path());
    return dcp::Copy(out->boot, paths.boot) && dcp::Copy(out->content_profile, paths.content_profile) &&
           dcp::Copy(out->app0, paths.app0) && dcp::Copy(out->image_dir, paths.image_dir) &&
           dcp::Copy(out->user_dir, paths.user_dir) && dcp::Copy(out->logs_dir, paths.logs_dir);
}
