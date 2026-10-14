#pragma once

#include <filesystem>
#include <string>

namespace dcp {

// Empty strings ask for the default.
struct PathRequest {
    std::string boot;
    std::string content_profile;
    std::string app0;
    std::string user_dir;
    std::string logs_dir;
};

struct Paths {
    std::filesystem::path boot;
    std::filesystem::path content_profile;
    std::filesystem::path app0;
    std::filesystem::path image_dir;
    std::filesystem::path user_dir;
    std::filesystem::path logs_dir;
};

// Explicit paths win. The rest are next to the executable: game\prepared\v1.03, game\image\v1.03, user and logs; or,
// for an executable in build\cmake\<toolchain>\bin, the repository's game\prepared\v1.03, build\image\v1.03, user and
// build\logs.
Paths ResolvePaths(const PathRequest& request, const std::filesystem::path& exe_dir);

} // namespace dcp
