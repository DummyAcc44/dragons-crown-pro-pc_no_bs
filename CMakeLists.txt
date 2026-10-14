cmake_minimum_required(VERSION 3.24)
project(dragons_crown_pro_pc LANGUAGES C)

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

find_package(SDL3 CONFIG REQUIRED)

set(DCP_RUNTIME_SOURCES
    src/runtime.c
    src/runtime_audio.c
    src/runtime_content.c
    src/runtime_fiber.c
    src/runtime_file.c
    src/runtime_kernel.c
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

# The video core (gpu/) is not vendored yet; gpu_stub.c implements src/dcpgpu.h without graphics.
set(DCP_GPU_SOURCES src/gpu_stub.c)

enable_language(RC)
add_executable(dcp-loader src/loader.c ${DCP_RUNTIME_SOURCES} ${DCP_GPU_SOURCES} src/compat_win.c src/dcp-loader.rc)
target_include_directories(dcp-loader PRIVATE src)
target_compile_options(dcp-loader PRIVATE -Wall -Wextra -Werror -fno-omit-frame-pointer)
# Windows 10 1903+ APIs (VirtualAlloc2, MapViewOfFile3).
target_compile_definitions(dcp-loader PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000007)
# The PS4 user range (64 GiB - 1 TiB) must stay free of host allocations: no high-entropy ASLR.
# The guest main thread runs on the process stack: 16 MiB, committed (guest code has no stack probes).
target_link_options(dcp-loader PRIVATE -Wl,--disable-high-entropy-va "SHELL:-Xlinker --stack -Xlinker 16777216,16777216")
target_link_libraries(dcp-loader PRIVATE SDL3::SDL3 onecore bcrypt ws2_32 -pthread)
