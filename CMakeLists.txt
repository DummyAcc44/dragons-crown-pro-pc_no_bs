cmake_minimum_required(VERSION 3.24)
project(dragons_crown_pro_pc LANGUAGES C CXX)
set(DCP_PORT_VERSION "0.1.0")

if (NOT WIN32 OR NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Build with MSYS2 CLANG64 (tools\build.ps1).")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# Repository paths in __FILE__ and std::source_location stay out of the binary; debug info keeps them.
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=>")

if (NOT DCP_PORT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    message(FATAL_ERROR "DCP_PORT_VERSION must start with MAJOR.MINOR.PATCH")
endif()
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/dcp_version.h CONTENT "#define DCP_VERSION_NUMBERS \
${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0\n#define DCP_FILE_VERSION_STRING \
\"${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}.0\"\n#define DCP_VERSION_STRING \"${DCP_PORT_VERSION}\"\n")

find_package(SDL3 CONFIG REQUIRED)
find_package(Freetype REQUIRED)

add_subdirectory(gpu)
add_subdirectory(launcher)

set(DCP_RUNTIME_SOURCES
    src/runtime.c
    src/runtime_audio.c
    src/runtime_content.c
    src/runtime_dcp.c
    src/runtime_eventflag.c
    src/runtime_fiber.c
    src/runtime_file.c
    src/runtime_font.c
    src/runtime_kernel.c
    src/runtime_log.c
    src/runtime_memory.c
    src/runtime_mutex.c
    src/runtime_pad.c
    src/runtime_rtc.c
    src/runtime_rwlock.c
    src/runtime_savedata.c
    src/runtime_sema.c
    src/runtime_services.c
    src/runtime_thread.c
    src/runtime_time.c)

enable_language(RC)
# Relink when the application icon, the manifest or the version changes.
set_source_files_properties(src/dcp-loader.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_SOURCE_DIR}/packaging/icons/app-icon.ico;${CMAKE_SOURCE_DIR}/src/dcp-loader.manifest;${CMAKE_BINARY_DIR}/generated/dcp_version.h")
add_executable(dcp-loader src/loader.c ${DCP_RUNTIME_SOURCES} src/compat_win.c src/dcp-loader.rc)
target_include_directories(dcp-loader PRIVATE src launcher ${CMAKE_BINARY_DIR}/generated)
target_compile_options(dcp-loader PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Werror -fno-omit-frame-pointer>)
# Windows 10 1903+ APIs (VirtualAlloc2, MapViewOfFile3).
target_compile_definitions(dcp-loader PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000007)
# The PS4 user range (64 GiB - 1 TiB) must stay free of host allocations: no high-entropy ASLR.
# The guest main thread runs on the process stack: 16 MiB, committed (guest code has no stack probes).
target_link_options(dcp-loader PRIVATE -Wl,--disable-high-entropy-va "SHELL:-Xlinker --stack -Xlinker 16777216,16777216")
target_link_libraries(dcp-loader PRIVATE dcp_launcher dcpgpu SDL3::SDL3 Freetype::Freetype onecore bcrypt ws2_32 -pthread)
