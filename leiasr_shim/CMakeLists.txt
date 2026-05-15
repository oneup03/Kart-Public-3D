# Build the LeiaSR shim DLL (leiasr_shim.dll) - an MSVC-built bridge between
# the MinGW64-built SRB2Kart engine and the LeiaSR Simulated Reality OpenGL
# weaver. The engine loads this DLL at runtime via LoadLibrary; absence is
# graceful (LeiaSR mode falls back to plain SbS).
#
# This is *not* wired into the main src/Makefile or src/CMakeLists.txt: the
# game is GCC and this is MSVC. Build it once after a fresh SDK install:
#
#   cd leiasr_shim
#   cmake -B build -A x64
#   cmake --build build --config Release
#
# The output leiasr_shim.dll ends up in build/Release/ and gets staged next
# to srb2kart.exe by build.sh's DLL-staging block (with --no-dlls=0).
#
# The SR SDK is vendored as a submodule at libs/LeiaSR64; that's the default
# LEIASR_SDKROOT below. Override with -DLEIASR_SDKROOT=... if pointing at a
# system-wide install instead.

cmake_minimum_required(VERSION 3.21)
project(leiasr_shim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to the vendored submodule. The env var $LEIASR_SDKROOT (set by the
# SDK's Install.bat) is intentionally NOT consulted: it points at the
# system-wide LeiaSR install whose SDK version may not match the one this
# shim was written against. Pass -DLEIASR_SDKROOT=... explicitly to override.
if(NOT DEFINED LEIASR_SDKROOT)
    set(LEIASR_SDKROOT "${CMAKE_CURRENT_SOURCE_DIR}/../libs/LeiaSR64"
        CACHE PATH "LeiaSR SDK root")
endif()
message(STATUS "leiasr_shim: LEIASR_SDKROOT = ${LEIASR_SDKROOT}")
if(NOT EXISTS "${LEIASR_SDKROOT}/lib/cmake")
    message(FATAL_ERROR
        "LEIASR_SDKROOT does not contain lib/cmake; expected the SDK layout "
        "from libs/LeiaSR64/. Did you forget `git submodule update --init "
        "libs/LeiaSR64`?")
endif()

list(APPEND CMAKE_PREFIX_PATH "${LEIASR_SDKROOT}/lib/cmake")
find_package(simulatedreality REQUIRED)
find_package(srOpenGL REQUIRED)

# Build platform suffix matches the SR SDK's CMake target naming convention.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(BUILD_PLATFORM 32)
endif()

add_library(leiasr_shim SHARED leiasr_shim.cpp)

target_link_libraries(leiasr_shim PRIVATE
    simulatedreality
    srOpenGL${BUILD_PLATFORM}::srOpenGL
)

# MSVC + extern "C" __declspec(dllexport) is enough to get the C ABI we
# want; we don't ship a .def file because nothing here is C++-mangled. The
# resulting export names are exactly srk_init / srk_weave / srk_shutdown,
# which is what r_stereo_leiasr.c GetProcAddresses.
set_target_properties(leiasr_shim PROPERTIES
    OUTPUT_NAME "leiasr_shim"
    PREFIX ""
)

# Quiet the SR-SDK-internal warnings; we already wrap the include site with
# a pragma push, this picks up anything else that drifts in.
if(MSVC)
    target_compile_options(leiasr_shim PRIVATE /W3 /wd4251 /wd4275)
endif()
