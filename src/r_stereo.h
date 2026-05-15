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
/// \file  r_stereo.h
/// \brief Stereoscopic 3D rendering subsystem

#ifndef __R_STEREO__
#define __R_STEREO__

#include "doomdef.h"
#include "command.h"

// Stereoscopic display modes.
// NOTE: these integer values are saved in config.cfg - never reorder or
// reuse them. Append new modes at the end.
// STEREO_OFF/SBS/TAB are also mirrored as HWR_STEREO_* in hardware/hw_drv.h
// (the internal render layouts the GL backend sees) - keep them in sync.
typedef enum
{
	STEREO_OFF               = 0,
	STEREO_SBS               = 1, // Side-by-Side
	STEREO_TAB               = 2, // Top-and-Bottom
	STEREO_ANAGLYPH          = 3, // Red/cyan, Dubois matrix composite
	STEREO_ROW_INTERLACED    = 4,
	STEREO_LEIASR            = 5, // Simulated Reality autostereoscopic
	STEREO_COLUMN_INTERLACED = 6,
	STEREO_CHECKERBOARD      = 7,

	STEREO_NUMMODES
} stereomode_t;

// Console variables. SRB2Kart never draws the upstream Doom-style crosshair
// (HU_DrawCrosshair is commented out in hu_stuff.c), so there's no separate
// crosshair-depth slider - everything HUD-shaped uses cv_stereohuddepth.
extern consvar_t cv_stereomode;
extern consvar_t cv_stereoipd;
extern consvar_t cv_stereofoclen;
extern consvar_t cv_stereoswap;
extern consvar_t cv_stereohuddepth;

// Set true while the D_Display eye loop is running, so NetUpdate (which can
// flip displayplayer and mutate mob state) holds off until the frame is done.
extern boolean net_stereo_render_in_progress;

void R_RegisterStereoVars(void);

// Mode queries. R_StereoMode() returns the *internal render* mode - user-facing
// modes are substituted down to STEREO_OFF/SBS/TAB so the eye loop and the
// SetStereoMode HWRAPI only ever see the small internal set.
INT32   R_StereoMode(void);
boolean R_StereoActive(void);
INT32   R_StereoNumEyes(void);

// Per-eye render bracket. `pass` is 0 (left/top region) or 1 (right/bottom).
void R_BeginStereoEye(INT32 pass);
void R_EndStereoEye(void);

// Current-eye state, valid between R_BeginStereoEye and R_EndStereoEye.
// The placement eye picks the viewport region (ignores Swap Eyes); the
// perspective eye drives the iod sign and HUD parallax (honors Swap Eyes).
INT8  R_GetCurrentEye(void);    // perspective eye: -1, 0, +1
INT8  R_GetPlacementEye(void);  // region eye: -1, 0, +1
float R_GetStereoIOD(void);     // signed world-unit eye separation
float R_GetStereoFocal(void);   // convergence-plane distance, world units

// Viewport rect for an eye, in GL (bottom-up Y) screen pixels.
// player_idx -1 = single-view layout (eye half of the whole screen).
void R_StereoComputePlayerEyeRect(INT32 mode, INT32 eye, INT32 player_idx,
	INT32 *x, INT32 *y, INT32 *w, INT32 *h);

// Run a transient-screen drawer (loading screen, quit screen, ...) once per
// eye, or once unchanged when stereo is off. drawfn must be self-contained -
// no state mutation, since it is invoked twice.
void R_DrawAcrossStereoEyes(void (*drawfn)(void));

// Backbuffer-is-stereo flag, consumed by the present path to decide whether
// to stretch the rendered backbuffer across the SDL window.
void    R_SetBackbufferIsStereo(boolean value);
boolean R_BackbufferIsStereo(void);

// HUD parallax. Returns a base-coord fixed_t x-shift for the current eye;
// zero when stereo is inactive or outside the eye loop.
fixed_t R_GetStereoHUDShift(void);

#endif // __R_STEREO__
