// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_stereo.c
/// \brief Stereoscopic 3D rendering subsystem
///
///        Engine-side stereo state: the display-mode CVARs, the eye-pass
///        bracket, the per-eye viewport layout helper, and the HUD parallax
///        helpers. The actual per-eye projection and viewport work lives in
///        the hardware renderer; this module only computes and hands out the
///        parameters that drive it.

#include <math.h>

#include "doomdef.h"
#include "command.h"
#include "doomstat.h" // splitscreen
#include "screen.h"   // vid, BASEVIDWIDTH
#include "i_video.h"  // rendermode, render_opengl
#include "console.h"  // CONS_Alert
#include "r_main.h"   // cv_fov
#include "r_stereo.h"

#ifdef HWRENDER
#include "hardware/hw_main.h" // HWR_SetStereoMode / HWR_ResetStereoMode
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ==========================================================================
//                                                          CONSOLE VARIABLES
// ==========================================================================

// The order is the user-facing menu cycle order. The integer values match
// the stereomode_t enum and are written to config.cfg, so never reorder or
// reuse them. LeiaSR sits at the end of the cycle - it silently falls back
// to plain SbS when leiasr_shim.dll or the SR runtime isn't present (see
// HWR_DrawStereoComposite in hw_main.c), so it's safe to expose
// unconditionally and still ship a working game without the SR hardware.
static CV_PossibleValue_t stereomode_cons_t[] = {
	{STEREO_OFF,               "Off"},
	{STEREO_SBS,               "Side-by-Side"},
	{STEREO_TAB,               "Top-and-Bottom"},
	{STEREO_ANAGLYPH,          "Anaglyph"},
	{STEREO_ROW_INTERLACED,    "Row-Interlaced"},
	{STEREO_COLUMN_INTERLACED, "Column-Interlaced"},
	{STEREO_CHECKERBOARD,      "Checkerboard"},
	{STEREO_LEIASR,            "LeiaSR"},
	{0, NULL}
};

// IPD slider holds tenths of a world unit (10..400 = 1.0..40.0 wu). The
// upper end is intentionally generous - kart play has wildly different
// world-unit scales than the games this engine descends from and players
// want a lot of room to push the apparent depth.
static CV_PossibleValue_t stereoipd_cons_t[]    = {{10, "MIN"}, {400, "MAX"}, {0, NULL}};
// Convergence plane distance, world units (50..250 wu).
static CV_PossibleValue_t stereofoclen_cons_t[] = {{50, "MIN"}, {250, "MAX"}, {0, NULL}};
// HUD depth slider holds hundredths of a parallax fraction (-1.00..+0.50).
// Negative values push the HUD behind the screen plane (away from the
// viewer), positive values pop it forward.
static CV_PossibleValue_t stereohuddepth_cons_t[] = {{-100, "MIN"}, {50, "MAX"}, {0, NULL}};

static void Stereo_OnChange(void);

consvar_t cv_stereomode     = {"stereomode",     "Off", CV_SAVE|CV_CALL, stereomode_cons_t,     Stereo_OnChange, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_stereoipd      = {"stereoipd",      "60",  CV_SAVE,         stereoipd_cons_t,      NULL,            0, NULL, NULL, 0, 0, NULL};  // x0.1 -> 6.0 wu IPD (~5x human-scale, clear depth pop at default focal)
consvar_t cv_stereofoclen   = {"stereofoclen",   "100", CV_SAVE,         stereofoclen_cons_t,   NULL,            0, NULL, NULL, 0, 0, NULL};  // x1.0 -> 100.0 wu convergence (typical scene viewing distance)
consvar_t cv_stereoswap     = {"stereoswap",     "Off", CV_SAVE,         CV_OnOff,              NULL,            0, NULL, NULL, 0, 0, NULL};
consvar_t cv_stereohuddepth = {"stereohuddepth", "-30", CV_SAVE,         stereohuddepth_cons_t, NULL,            0, NULL, NULL, 0, 0, NULL};  // -0.30 fraction - HUD pops slightly forward of screen

boolean net_stereo_render_in_progress = false;

// ==========================================================================
//                                                                EYE STATE
// ==========================================================================

static INT8  current_placement_eye = 0; // viewport region: -1 left/top, +1 right/bottom
static INT8  current_eye = 0;            // perspective eye: placement, Swap-adjusted
static float current_iod = 0.0f;         // signed world-unit eye separation
static float current_focal = 0.0f;       // convergence plane distance, world units
static boolean backbuffer_is_stereo = false;

// ==========================================================================
//                                                                  CVAR GLUE
// ==========================================================================

// This fork sets rendermode once at startup and has no cv_renderer toggle, so
// a single check is enough. CV_SAVE means we never snap the value back - we
// just warn that it will not take effect under the software renderer.
static void Stereo_OnChange(void)
{
	if (cv_stereomode.value != STEREO_OFF && rendermode != render_opengl)
		CONS_Alert(CONS_WARNING,
			"Stereoscopic 3D requires the OpenGL renderer; setting will take effect once OpenGL is selected.\n");
}

void R_RegisterStereoVars(void)
{
	CV_RegisterVar(&cv_stereomode);
	CV_RegisterVar(&cv_stereoipd);
	CV_RegisterVar(&cv_stereofoclen);
	CV_RegisterVar(&cv_stereoswap);
	CV_RegisterVar(&cv_stereohuddepth);
}

// ==========================================================================
//                                                              MODE QUERIES
// ==========================================================================

INT32 R_StereoMode(void)
{
	if (rendermode != render_opengl)
		return STEREO_OFF;

	// Substitute every user-facing mode down to the small set of internal
	// render formats. The shader-composite modes (anaglyph, interlaced,
	// checkerboard) render internally as SbS or TaB and are composited at
	// present time, so the eye loop and SetStereoMode never see them.
	switch (cv_stereomode.value)
	{
		case STEREO_SBS:
		case STEREO_TAB:
			return cv_stereomode.value;

		case STEREO_ANAGLYPH:
		case STEREO_COLUMN_INTERLACED:
		case STEREO_CHECKERBOARD:
		case STEREO_LEIASR:
			// LeiaSR collapses to SbS for the internal render - the SR weaver
			// takes a single full-screen side-by-side texture as input, and
			// has no notion of splitscreen anyway. The present path
			// (ogl_sdl.c) still dispatches on raw cv_stereomode.value so the
			// actual weave happens for LeiaSR and not the others; when the
			// shim DLL or SR runtime is absent, R_LeiaSR_Available() is false
			// there and we silently fall back to plain SbS present.
			return STEREO_SBS;

		case STEREO_ROW_INTERLACED:
			return STEREO_TAB;

		default:
			return STEREO_OFF;
	}
}

boolean R_StereoActive(void)
{
	return R_StereoMode() != STEREO_OFF;
}

INT32 R_StereoNumEyes(void)
{
	return R_StereoActive() ? 2 : 1;
}

// ==========================================================================
//                                                          EYE-PASS BRACKET
// ==========================================================================

void R_BeginStereoEye(INT32 pass)
{
	if (!R_StereoActive())
	{
		// Mono frame: leave every eye field at zero so consumers (the
		// FTransform population, HUD shift) stay on the unchanged path.
		R_EndStereoEye();
		return;
	}

	current_placement_eye = (pass == 0) ? -1 : 1;
	current_eye = cv_stereoswap.value
		? (INT8)(-current_placement_eye)
		: current_placement_eye;
	current_focal = (float)cv_stereofoclen.value;
	current_iod = (float)current_eye * ((float)cv_stereoipd.value * 0.1f);
}

void R_EndStereoEye(void)
{
	current_placement_eye = 0;
	current_eye = 0;
	current_iod = 0.0f;
	current_focal = 0.0f;
}

INT8  R_GetCurrentEye(void)   { return current_eye; }
INT8  R_GetPlacementEye(void) { return current_placement_eye; }
float R_GetStereoIOD(void)    { return current_iod; }
float R_GetStereoFocal(void)  { return current_focal; }

// ==========================================================================
//                                                            EYE VIEWPORT
// ==========================================================================

// Returns the GL viewport rect for one eye. Y is bottom-up, GL convention.
// The layout is always eye-outer / player-inner: first the screen is split
// into the eye's half, then - if this is a splitscreen player - that half is
// subdivided exactly the way HWR_RenderFrame subdivides the full screen for
// mono splitscreen. player_idx -1 (or no splitscreen active) means the eye's
// whole half. Eye-outer keeps each eye's content in one contiguous region,
// which is what SbS/TaB displays (and the composite shaders) expect.
void R_StereoComputePlayerEyeRect(INT32 mode, INT32 eye, INT32 player_idx,
	INT32 *x, INT32 *y, INT32 *w, INT32 *h)
{
	const INT32 fw = vid.width;
	const INT32 fh = vid.height;
	INT32 ex, ey, ew, eh; // the eye's half of the whole screen

	// Step 1: the eye's half of the screen.
	switch (mode)
	{
		case STEREO_SBS:
			ex = (eye == 0) ? 0 : fw / 2;
			ey = 0;
			ew = fw / 2;
			eh = fh;
			break;

		case STEREO_TAB:
			// Eye 0 (left) goes in the TOP half; GL Y is bottom-up.
			ex = 0;
			ey = (eye == 0) ? fh / 2 : 0;
			ew = fw;
			eh = fh / 2;
			break;

		default: // STEREO_OFF or unrecognised: the whole screen, mono.
			ex = 0;
			ey = 0;
			ew = fw;
			eh = fh;
			break;
	}

	// Step 2: place this player within the eye's half. With no splitscreen
	// player (player_idx < 0) or splitscreen inactive, the player fills the
	// whole half.
	if (player_idx < 0 || splitscreen == 0)
	{
		*x = ex;
		*y = ey;
		*w = ew;
		*h = eh;
	}
	else if (splitscreen == 1)
	{
		// Two players stacked: P0 top, P1 bottom (GL Y is bottom-up).
		*x = ex;
		*w = ew;
		*h = eh / 2;
		*y = (player_idx == 0) ? ey + eh / 2 : ey;
	}
	else
	{
		// Three or four players, 2x2: P0 top-left, P1 top-right,
		// P2 bottom-left, P3 bottom-right - matching HWR_RenderFrame.
		*w = ew / 2;
		*h = eh / 2;
		*x = ex + ((player_idx & 1) ? ew / 2 : 0);
		*y = ey + ((player_idx < 2) ? eh / 2 : 0);
	}
}

// ==========================================================================
//                                                        TRANSIENT SCREENS
// ==========================================================================

// Loading screens, the quit screen, the pre-wipe fade fill - these draw
// outside the D_Display eye loop, so they need their own per-eye pass.
void R_DrawAcrossStereoEyes(void (*drawfn)(void))
{
#ifdef HWRENDER
	if (R_StereoActive())
	{
		INT32 eye;

		for (eye = 0; eye < R_StereoNumEyes(); eye++)
		{
			INT32 ex, ey, ew, eh;
			R_StereoComputePlayerEyeRect(R_StereoMode(), eye, -1, &ex, &ey, &ew, &eh);
			HWR_SetStereoMode(R_StereoMode(), eye, ex, ey, ew, eh);
			R_BeginStereoEye(eye);
			drawfn();
			R_EndStereoEye();
		}

		HWR_ResetStereoMode();
		R_SetBackbufferIsStereo(true);
		return;
	}
#endif

	// Mono (or a renderer with no hardware path): draw once, unchanged.
	drawfn();
	R_SetBackbufferIsStereo(false);
}

// ==========================================================================
//                                                       PRESENT-PATH FLAG
// ==========================================================================

void    R_SetBackbufferIsStereo(boolean value) { backbuffer_is_stereo = value; }
boolean R_BackbufferIsStereo(void)             { return backbuffer_is_stereo; }

// ==========================================================================
//                                                             HUD PARALLAX
// ==========================================================================

// Per-eye horizontal HUD shift for a given parallax fraction, in base
// (320x200) coordinate fixed-point units so it can be added straight onto a
// V_Draw* x argument. depth_frac > 0 pushes the HUD behind the screen plane.
static fixed_t R_StereoHUDShiftForDepth(float depth_frac)
{
	double fov_deg, tan_half, disparity_base, shift_base;

	if (current_eye == 0 || current_focal <= 0.0f)
		return 0;

	fov_deg = FIXED_TO_FLOAT(cv_fov.value);
	tan_half = tan(fov_deg * M_PI / 360.0);
	disparity_base = ((double)cv_stereoipd.value * 0.1) * (double)BASEVIDWIDTH
		/ (4.0 * (double)current_focal * tan_half);

	shift_base = -(double)current_eye * (double)depth_frac * disparity_base;
	return (fixed_t)(shift_base * FRACUNIT);
}

fixed_t R_GetStereoHUDShift(void)
{
	return R_StereoHUDShiftForDepth((float)cv_stereohuddepth.value / 100.0f);
}
