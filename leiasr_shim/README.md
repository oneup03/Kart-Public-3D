# LeiaSR Shim DLL

A small MSVC-built bridge DLL (`leiasr_shim.dll`) that lets the MinGW64-built
SRB2Kart engine drive the LeiaSR / Simulated Reality OpenGL weaver. The
engine loads this DLL at runtime; absence is graceful — LeiaSR stereo mode
silently falls back to plain Side-by-Side.

## Why a separate DLL?

The Simulated Reality SDK ships only MSVC import libraries, and its
high-level `SR::GLWeaver` is a C++ class with virtual inheritance whose ABI
differs between MSVC and the Itanium ABI MinGW follows. We can't link the
SR `.lib` files directly into the MinGW build, and we can't wrap them with
a C facade at link time either. The shim is the smallest possible MSVC
translation unit: it owns the C++ side (the `SR::SRContext` and
`SR::IGLWeaver1` instances), and exposes a flat C ABI:

```c
int  srk_init(void *hwnd);             // 1 = success, 0 = unavailable
void srk_weave(unsigned tex_id, int w, int h);
void srk_shutdown(void);
```

The engine-side loader is `src/r_stereo_leiasr.{c,h}` (pure C, MinGW). It
`LoadLibrary`s `leiasr_shim.dll`, `GetProcAddress`es those three symbols,
and treats any failure (DLL missing, SR runtime not installed, no SR
display attached) as "LeiaSR unavailable".

## Build prerequisites

1. **Visual Studio 2022** (or any MSVC toolchain CMake can find). Install
   the "Desktop development with C++" workload.
2. **LeiaSR SDK.** The repo's `libs/LeiaSR64` submodule vendors a copy.
   Make sure it's checked out:
   ```
   git submodule update --init libs/LeiaSR64
   ```
   The shim defaults to that vendored copy. To build against a system-wide
   install instead, set `LEIASR_SDKROOT` (env var or `-D`).
3. **CMake ≥ 3.21.**

## Build steps

From this `leiasr_shim/` directory:

```cmd
cmake -B build -A x64
cmake --build build --config Release
```

That produces `build/Release/leiasr_shim.dll`. Stage it next to
`srb2kart.exe` (the main `build.sh` does this automatically when it sees
the DLL exists; see "Runtime layout" below).

## Runtime layout

For LeiaSR to actually weave, all of these must live next to
`srb2kart.exe`:

- `leiasr_shim.dll` (built above)
- The SR runtime DLLs (`DimencoWeaving.dll`, `SimulatedRealityCore.dll`,
  `SimulatedRealityOpenGL.dll`, OpenCV runtime, etc.). These come from a
  system-wide LeiaSR install via `%LEIASR_SDKROOT%\bin`; the easiest path
  is to install the SR runtime as documented in `libs/LeiaSR64/Readme.txt`
  and let `%LEIASR_SDKROOT%\bin` be on `PATH`.

If the shim DLL or any of its SR-side dependencies fail to load, the engine
silently falls back to plain SbS — no error, no crash, no menu changes.
This is by design: most users don't have LeiaSR hardware.

## Smoke test

With everything staged, run `srb2kart.exe` and switch
`Options → Video → Stereoscopic 3D… → Display Mode` to **LeiaSR**.

- On a Leia / SR display, you should see autostereoscopic 3D without
  glasses.
- On a normal monitor with the shim DLL absent, you should see plain
  Side-by-Side (the substituted fallback). Tilting `cv_stereoipd` should
  produce parallax exactly as in SbS mode.
- On a normal monitor with the shim DLL present but no SR display, you
  should also see plain SbS — the shim's `srk_init` catches the SR
  exception and reports unavailable.
