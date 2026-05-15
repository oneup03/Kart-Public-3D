#!/usr/bin/env bash
#
# build.sh - Build SRB2Kart 3D (64-bit, MinGW + SDL2/OpenGL)
#
# Works both natively on Windows (git-bash + MinGW-w64) and when
# cross-compiling from Linux (gcc-mingw-w64-x86-64-win32). It rebuilds the
# stale bundled libpng/zlib static libs, then builds
# bin/Mingw64/Release/srb2kart.exe and stages the runtime DLLs next to it.
# If a ./testing folder exists (local install with game assets), the exe and
# DLLs are also copied there so the build is immediately runnable.
#
# Usage:
#   ./build.sh [--clean] [--no-dlls] [--no-shim] [--jobs N]
#
# Options:
#   --clean      run "make clean" for the Mingw64 target before building
#   --no-dlls    skip staging the runtime DLLs (output dir and testing folder)
#   --no-shim    skip the optional LeiaSR shim DLL build (MSVC-only step)
#   --jobs N     parallel make jobs (default: number of CPUs)
#
# Environment overrides:
#   PREFIX   toolchain prefix (default: x86_64-w64-mingw32)
#   JOBS     parallel jobs (default: nproc)
#   TESTDIR  local play-test folder to also copy exe+DLLs into (default: testing)
#
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

# Convenience: on Windows, add the default choco MinGW-w64 location if present.
[ -d /c/ProgramData/mingw64/mingw64/bin ] && PATH="/c/ProgramData/mingw64/mingw64/bin:$PATH"

PREFIX="${PREFIX:-x86_64-w64-mingw32}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
DO_CLEAN=0
COPY_DLLS=1
DO_SHIM=1

while [ $# -gt 0 ]; do
	case "$1" in
		--clean)    DO_CLEAN=1 ;;
		--no-dlls)  COPY_DLLS=0 ;;
		--no-shim)  DO_SHIM=0 ;;
		--jobs)     JOBS="$2"; shift ;;
		--jobs=*)   JOBS="${1#*=}" ;;
		-h|--help)  sed -n '2,/^$/p' "$0" | sed 's/^# \?//'; exit 0 ;;
		*) echo "build.sh: unknown option: $1" >&2; exit 1 ;;
	esac
	shift
done

# Initialize the LeiaSR64 submodule on demand - only when we'd actually use
# it for the shim build. CI runs this script with --no-shim and supplies a
# prebuilt leiasr_shim.dll via artifact, so it doesn't need the submodule at
# all (and doesn't have credentials for it on a public PAT-less runner).
if [ "$DO_SHIM" = 1 ] && [ -f .gitmodules ] && \
   [ ! -e libs/LeiaSR64/Readme.txt ] && command -v git >/dev/null 2>&1; then
	echo ">> initializing libs/LeiaSR64 submodule"
	git submodule update --init libs/LeiaSR64 2>/dev/null || \
		echo "   (skipped - submodule init failed; LeiaSR shim DLL will not build)"
fi

# Resolve toolchain binaries: prefer the prefixed names (Linux cross-compiler),
# fall back to unprefixed (a native Windows MinGW-w64 install ships those).
pick() { if command -v "${PREFIX}-$1" >/dev/null 2>&1; then echo "${PREFIX}-$1"; else echo "$1"; fi; }
CC=$(pick gcc)
AR=$(pick ar)
RANLIB=$(pick ranlib)
WINDRES=$(pick windres)
OBJCOPY=$(pick objcopy)
STRIP=$(pick strip)

command -v "$CC" >/dev/null || { echo "build.sh: '$CC' not found in PATH" >&2; exit 1; }
echo ">> toolchain : $("$CC" --version | head -1)"
echo ">> jobs      : $JOBS"

# 1. Rebuild bundled zlib (libz64.a).
#    The committed libz64.a was linked against an old MinGW runtime and
#    references MSVCRT symbols (_vsnprintf) that current mingw-w64 import
#    libraries no longer expose, so it must be rebuilt from source.
echo ">> rebuilding libs/zlib/win32/libz64.a"
ZLIB_SRCS="adler32 compress crc32 deflate gzclose gzlib gzread gzwrite \
           infback inffast inflate inftrees trees uncompr zutil"
(
	cd libs/zlib
	rm -f ./*.o
	for f in $ZLIB_SRCS; do "$CC" -c -O3 -DNO_VIZ "$f.c" -o "$f.o"; done
	rm -f win32/libz64.a
	"$AR" rcs win32/libz64.a ./*.o
	"$RANLIB" win32/libz64.a
	rm -f ./*.o
)

# 2. Rebuild bundled libpng (libpng64.a).
#    Same problem: the committed libpng64.a references __iob_func.
echo ">> rebuilding libs/libpng-src/projects/libpng64.a"
PNG_SRCS="png pngerror pngget pngmem pngpread pngread pngrio pngrtran \
          pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil"
(
	cd libs/libpng-src
	rm -f ./*.o
	for f in $PNG_SRCS; do "$CC" -c -O3 -I../zlib "$f.c" -o "$f.o"; done
	rm -f projects/libpng64.a
	"$AR" rcs projects/libpng64.a ./*.o
	"$RANLIB" projects/libpng64.a
	rm -f ./*.o
)

# 3. Build srb2kart.exe.
if [ "$DO_CLEAN" = 1 ]; then
	echo ">> make clean"
	make -C src MINGW64=1 SDL=1 clean >/dev/null 2>&1 || true
fi

echo ">> building srb2kart.exe"
# -std=gnu17: the codebase predates the C23 "() means (void)" change that
# became the default in GCC 14+. Passed via CPPFLAGS (not OPTS=) so the
# Makefile's own OPTS+= accumulations (e.g. -DHAVE_BLUA) still apply.
export CPPFLAGS="-std=gnu17"
make -C src -j"$JOBS" \
	MINGW64=1 SDL=1 NOUPX=1 WARNINGMODE=1 \
	CC="$CC" WINDRES="$WINDRES" OBJCOPY="$OBJCOPY" STRIP="$STRIP"

OUT=bin/Mingw64/Release
[ -f "$OUT/srb2kart.exe" ] || { echo "build.sh: expected $OUT/srb2kart.exe, not found" >&2; exit 1; }
echo ">> built     : $OUT/srb2kart.exe"

# 4. Build the optional LeiaSR shim DLL (MSVC). See leiasr_shim/README.md for
#    what this is. CMake auto-picks an installed Visual Studio toolchain on
#    Windows; on Linux the configure step has no valid Win32 generator and
#    will fail cleanly, which we treat as "no shim, fall back to SbS". Any
#    failure here is non-fatal - the staging block below just won't find the
#    DLL, and LeiaSR stereo mode silently degrades to plain Side-by-Side.
if [ "$DO_SHIM" = 1 ] && [ -f leiasr_shim/CMakeLists.txt ]; then
	if command -v cmake >/dev/null 2>&1; then
		echo ">> building LeiaSR shim DLL (MSVC)"
		# Configure separately from build so we can distinguish "no MSVC
		# toolchain at all" from "shim source broke". -A x64 implies the
		# Visual Studio generator; on non-Windows this fails fast.
		if cmake -S leiasr_shim -B leiasr_shim/build -A x64 >/tmp/shim-configure.log 2>&1; then
			if cmake --build leiasr_shim/build --config Release >/tmp/shim-build.log 2>&1; then
				echo "   shim built  : leiasr_shim/build/Release/leiasr_shim.dll"
			else
				echo "   WARN: shim compile failed (see /tmp/shim-build.log); LeiaSR mode will fall back to SbS"
			fi
		else
			# Quiet during cross-compile / missing-MSVC - this is expected on
			# Linux CI runners. Loud when CMake is present but VS isn't, so
			# a Windows user notices their toolchain is missing.
			if [ "${OSTYPE:-}" = "msys" ] || [ "${OSTYPE:-}" = "cygwin" ] || \
			   [ "${OS:-}" = "Windows_NT" ]; then
				echo "   WARN: shim configure failed (see /tmp/shim-configure.log); is Visual Studio installed?"
				echo "         (LeiaSR mode will fall back to SbS)"
			fi
		fi
	fi
fi

# 5. Stage runtime DLLs next to the executable, and into the play-test folder.
TESTDIR="${TESTDIR:-testing}"
DLLS=(
	libs/curl/lib64/libcurl-x64.dll
	libs/dll-binaries/x86_64/discord-rpc.dll
	libs/dll-binaries/x86_64/libgme.dll
	libs/SDL2/x86_64-w64-mingw32/bin/SDL2.dll
	libs/SDL2_mixer/x86_64-w64-mingw32/bin/*.dll
)

# Optional LeiaSR shim - built separately (leiasr_shim/CMakeLists.txt, MSVC).
# Absence is fine; the engine's runtime loader treats it as "LeiaSR
# unavailable" and falls back to plain SbS for that mode. We probe both the
# default CMake output dir and a flat leiasr_shim.dll the user dropped at
# the shim project root, in that order.
LEIASR_SHIMS=()
for cand in \
	leiasr_shim/build/Release/leiasr_shim.dll \
	leiasr_shim/leiasr_shim.dll \
; do
	if [ -f "$cand" ]; then
		LEIASR_SHIMS=("$cand")
		break
	fi
done

if [ "$COPY_DLLS" = 1 ]; then
	echo ">> staging runtime DLLs into $OUT"
	cp -f "${DLLS[@]}" "$OUT/"

	if [ ${#LEIASR_SHIMS[@]} -gt 0 ]; then
		echo ">> staging LeiaSR shim DLL (${LEIASR_SHIMS[0]})"
		cp -f "${LEIASR_SHIMS[@]}" "$OUT/"
	fi

	if [ -d "$TESTDIR" ]; then
		echo ">> copying srb2kart.exe + DLLs into $TESTDIR/"
		cp -f "$OUT/srb2kart.exe" "${DLLS[@]}" "$TESTDIR/"
		if [ ${#LEIASR_SHIMS[@]} -gt 0 ]; then
			cp -f "${LEIASR_SHIMS[@]}" "$TESTDIR/"
		fi
	fi
fi

echo ">> done."
