// SONIC ROBO BLAST 2 - Kart-Public-3D
//-----------------------------------------------------------------------------
// MSVC-built shim around the LeiaSR Simulated Reality OpenGL weaver.
//
// The SRB2Kart engine is built with MinGW64 and cannot link directly against
// the SR SDK's MSVC import libraries - the high-level `GLWeaver` is a C++
// class with virtual inheritance, whose ABI differs between MSVC and the
// Itanium ABI MinGW follows. This shim is the smallest possible MSVC TU
// that owns the C++ side: it links against the SR libs, owns the
// `SR::SRContext` and `SR::IGLWeaver1` instances, and exposes a flat C ABI
// that the engine (r_stereo_leiasr.c) loads at runtime via LoadLibrary.
//
// Build: see leiasr_shim/CMakeLists.txt or leiasr_shim/README.md - the
// resulting leiasr_shim.dll is staged next to srb2kart.exe by build.sh.
// When the DLL is absent (no LeiaSR runtime), the engine silently falls
// back to plain Side-by-Side.
//-----------------------------------------------------------------------------

#include <cstring>

// Suppress benign warnings from inside the SR headers (they use a few
// deprecated patterns and don't compile clean under /W4).
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251 4275 4267)
#endif

#include <sr/management/srcontext.h>
#include <sr/weaver/glweaver.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// We sample the recaptured backbuffer texture, which r_opengl.c creates with
// internal format GL_RGB. GL constants are not pulled in by the SR headers
// (they only forward-declare GLuint/GLenum), so we just hardcode the value.
#ifndef GL_RGB8
#define GL_RGB8 0x8051
#endif

namespace
{
	// One context + one weaver, kept alive across frames. The shim assumes a
	// single-window game; if multiple windows ever become a thing, this would
	// need to become a handle-keyed table.
	SR::SRContext   *g_ctx    = nullptr;
	SR::IGLWeaver1  *g_weaver = nullptr;

	// Cached dimensions of the input view texture so we only push
	// setInputViewTexture when something actually changes.
	unsigned         g_lastTex = 0;
	int              g_lastW   = 0;
	int              g_lastH   = 0;
}

extern "C" __declspec(dllexport)
int srk_init(void *hwnd)
{
	if (g_weaver != nullptr)
		return 1; // Already up.

	// SR::SRContext::create() raises ServerNotAvailableException when the SR
	// service isn't reachable - the most common cause is "no Leia display
	// connected" on a development machine. SR::CreateGLWeaver can also fail
	// (returns an error code rather than throwing) when there is a context
	// but no weaving-capable display. Either way we want to return cleanly
	// to C without surfacing the C++ exception across the ABI boundary.
	try
	{
		g_ctx = SR::SRContext::create();
		if (g_ctx == nullptr)
			return 0;

		HWND hwndNative = static_cast<HWND>(hwnd);
		WeaverErrorCode rc = SR::CreateGLWeaver(*g_ctx, hwndNative, &g_weaver);
		if (rc != WeaverSuccess || g_weaver == nullptr)
		{
			SR::SRContext::deleteSRContext(g_ctx);
			g_ctx = nullptr;
			g_weaver = nullptr;
			return 0;
		}

		// CRITICAL: this is the step that actually starts the face/eye
		// trackers behind the SR context. Without it, weave() returns the
		// view texture warped only by static lens geometry - no head
		// tracking, no parallax that follows the viewer. The SDK's OpenGL
		// example does this *after* the weaver is constructed so the
		// weaver's tracker subscriptions are already in place when the
		// context spins up.
		g_ctx->initialize();
	}
	catch (...)
	{
		// Drop the partial state. Any future srk_init call will retry from
		// scratch, but the engine-side loader latches on first attempt so
		// in practice this path is hit exactly once on machines without SR.
		if (g_weaver) { g_weaver->destroy(); g_weaver = nullptr; }
		if (g_ctx)    { SR::SRContext::deleteSRContext(g_ctx); g_ctx = nullptr; }
		return 0;
	}

	return 1;
}

extern "C" __declspec(dllexport)
void srk_weave(unsigned int tex_id, int width, int height)
{
	if (g_weaver == nullptr || tex_id == 0)
		return;

	try
	{
		// Avoid pushing the input texture every frame when nothing changed -
		// the SR runtime allocates internal resources keyed to dimensions, so
		// a redundant rebind would waste work.
		if (tex_id != g_lastTex || width != g_lastW || height != g_lastH)
		{
			g_weaver->setInputViewTexture(tex_id, width, height, GL_RGB8);
			g_lastTex = tex_id;
			g_lastW   = width;
			g_lastH   = height;
		}

		g_weaver->weave();
	}
	catch (...)
	{
		// Swallow - one bad frame shouldn't tear down the renderer. If the
		// SR runtime is in a permanently broken state, every subsequent
		// weave will hit the same catch and the user just sees the
		// pre-woven SbS image (still correct, just unwoven).
	}
}

extern "C" __declspec(dllexport)
void srk_shutdown(void)
{
	try
	{
		if (g_weaver) { g_weaver->destroy(); g_weaver = nullptr; }
		if (g_ctx)    { SR::SRContext::deleteSRContext(g_ctx); g_ctx = nullptr; }
	}
	catch (...)
	{
		// Final cleanup - leak rather than crash on the way out.
		g_weaver = nullptr;
		g_ctx = nullptr;
	}
	g_lastTex = 0;
	g_lastW = 0;
	g_lastH = 0;
}
