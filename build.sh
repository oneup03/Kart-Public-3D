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
#   ./build.sh [--clean] [--no-dlls] [--jobs N]
#
# Options:
#   --clean      run "make clean" for the Mingw64 target before building
#   --no-dlls    skip staging the runtime DLLs (output dir and testing folder)
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

while [ $# -gt 0 ]; do
	case "$1" in
		--clean)    DO_CLEAN=1 ;;
		--no-dlls)  COPY_DLLS=0 ;;
		--jobs)     JOBS="$2"; shift ;;
		--jobs=*)   JOBS="${1#*=}" ;;
		-h|--help)  sed -n '2,/^$/p' "$0" | sed 's/^# \?//'; exit 0 ;;
		*) echo "build.sh: unknown option: $1" >&2; exit 1 ;;
	esac
	shift
done

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

# 4. Stage runtime DLLs next to the executable, and into the play-test folder.
TESTDIR="${TESTDIR:-testing}"
DLLS=(
	libs/curl/lib64/libcurl-x64.dll
	libs/dll-binaries/x86_64/discord-rpc.dll
	libs/dll-binaries/x86_64/libgme.dll
	libs/SDL2/x86_64-w64-mingw32/bin/SDL2.dll
	libs/SDL2_mixer/x86_64-w64-mingw32/bin/*.dll
)
if [ "$COPY_DLLS" = 1 ]; then
	echo ">> staging runtime DLLs into $OUT"
	cp -f "${DLLS[@]}" "$OUT/"

	if [ -d "$TESTDIR" ]; then
		echo ">> copying srb2kart.exe + DLLs into $TESTDIR/"
		cp -f "$OUT/srb2kart.exe" "${DLLS[@]}" "$TESTDIR/"
	fi
fi

echo ">> done."
