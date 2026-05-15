// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2019 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file
/// \brief imports/exports for the GPU hardware low-level interface API

#ifndef __HWR_DRV_H__
#define __HWR_DRV_H__

// this must be here 19991024 by Kin
#include "../screen.h"
#include "hw_data.h"
#include "hw_defs.h"
#include "hw_md2.h"

#include "hw_dll.h"

// ==========================================================================
//                                                       STANDARD DLL EXPORTS
// ==========================================================================

EXPORT boolean HWRAPI(Init) (void);
#if defined (PURESDL) || defined (macintosh)
EXPORT void HWRAPI(SetPalette) (INT32 *, RGBA_t *gamma);
#else
EXPORT void HWRAPI(SetPalette) (RGBA_t *ppal, RGBA_t *pgamma);
#endif
EXPORT void HWRAPI(FinishUpdate) (INT32 waitvbl);
EXPORT void HWRAPI(Draw2DLine) (F2DCoord *v1, F2DCoord *v2, RGBA_t Color);
EXPORT void HWRAPI(DrawPolygon) (FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags);
EXPORT void HWRAPI(SetBlend) (FBITFIELD PolyFlags);
EXPORT void HWRAPI(ClearBuffer) (FBOOLEAN ColorMask, FBOOLEAN DepthMask, FRGBAFloat *ClearColor);
EXPORT void HWRAPI(SetTexture) (FTextureInfo *TexInfo);
EXPORT void HWRAPI(ReadRect) (INT32 x, INT32 y, INT32 width, INT32 height, INT32 dst_stride, UINT16 *dst_data);
EXPORT void HWRAPI(GClipRect) (INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip);
EXPORT void HWRAPI(ClearMipMapCache) (void);

//Hurdler: added for backward compatibility
EXPORT void HWRAPI(SetSpecialState) (hwdspecialstate_t IdState, INT32 Value);

//Hurdler: added for new development
EXPORT void HWRAPI(DrawModel) (model_t *model, INT32 frameIndex, float duration, float tics, INT32 nextFrameIndex, FTransform *pos, float scale, UINT8 flipped, FSurfaceInfo *Surface);
EXPORT void HWRAPI(CreateModelVBOs) (model_t *model);
EXPORT void HWRAPI(SetTransform) (FTransform *stransform);
EXPORT INT32 HWRAPI(GetTextureUsed) (void);

EXPORT void HWRAPI(RenderSkyDome) (INT32 tex, INT32 texture_width, INT32 texture_height, FTransform transform);

EXPORT void HWRAPI(FlushScreenTextures) (void);
EXPORT void HWRAPI(StartScreenWipe) (void);
EXPORT void HWRAPI(EndScreenWipe) (void);
EXPORT void HWRAPI(DoScreenWipe) (float hudshift);
EXPORT void HWRAPI(DrawIntermissionBG) (void);
EXPORT void HWRAPI(MakeScreenTexture) (void);
EXPORT void HWRAPI(MakeScreenFinalTexture) (void);
EXPORT void HWRAPI(DrawScreenFinalTexture) (int width, int height, boolean stretch);

#define SCREENVERTS 10
EXPORT void HWRAPI(PostImgRedraw) (float points[SCREENVERTS][SCREENVERTS][2]);

// jimita
EXPORT boolean HWRAPI(LoadShaders) (void);
EXPORT void HWRAPI(KillShaders) (void);
EXPORT void HWRAPI(SetShader) (int shader);
EXPORT void HWRAPI(UnSetShader) (void);

EXPORT void HWRAPI(LoadCustomShader) (int number, char *shader, size_t size, boolean fragment);
EXPORT void HWRAPI(InitCustomShaders) (void);

EXPORT void HWRAPI(StartBatching) (void);
EXPORT void HWRAPI(RenderBatches) (int *sNumPolys, int *sNumVerts, int *sNumCalls, int *sNumShaders, int *sNumTextures, int *sNumPolyFlags, int *sNumColors);

// Stereoscopic 3D: internal render layouts the GL backend ever sees.
// R_StereoMode() substitutes every user-facing mode down to one of these
// before SetStereoMode is called. The values mirror stereomode_t in
// r_stereo.h (STEREO_OFF/SBS/TAB) and must stay in sync.
#define HWR_STEREO_OFF 0
#define HWR_STEREO_SBS 1
#define HWR_STEREO_TAB 2

// Stereoscopic 3D: per-eye viewport+scissor. The exact rect is computed
// engine-side (R_StereoComputePlayerEyeRect), so these just apply it -
// no per-eye color mask, no stencil.
EXPORT void HWRAPI(SetStereoMode) (INT32 mode, INT32 eye, INT32 x, INT32 y, INT32 w, INT32 h);
EXPORT void HWRAPI(ReapplyStereoMode) (void);
EXPORT void HWRAPI(ResetStereoMode) (void);

// Stereoscopic 3D: present-time composite shader indices. Must match the
// positions of the matching GLSL_*_COMPOSITE_FRAGMENT_SHADER entries in
// r_opengl.c's fragment_shaders[] / vertex_shaders[] arrays.
#define HWR_SHADER_COMPOSITE_ANAGLYPH_DUBOIS   8
#define HWR_SHADER_COMPOSITE_ROW_INTERLACED    9
#define HWR_SHADER_COMPOSITE_COLUMN_INTERLACED 10
#define HWR_SHADER_COMPOSITE_CHECKERBOARD      11

// Stereoscopic 3D: recapture the framebuffer at exactly w x h (NPOT) into a
// dedicated texture, and run a composite fragment shader on a fullscreen
// quad sampling that texture. The composite picks per-pixel which eye's half
// of the SbS/TaB source to read from (or mixes both via the Dubois matrix).
// MakeScreenTextureSized returns the GL texture id (typed as unsigned int so
// hw_drv.h does not need to drag the GL header in) so the LeiaSR present path
// can hand it to the SR weaver - it's a no-op return for the composite path.
EXPORT unsigned int HWRAPI(MakeScreenTextureSized) (int width, int height);
EXPORT void HWRAPI(DrawInterlacedComposite) (int shaderindex, int width, int height);

// ==========================================================================
//                                      HWR DRIVER OBJECT, FOR CLIENT PROGRAM
// ==========================================================================

#if !defined (_CREATE_DLL_)

struct hwdriver_s
{
	Init                pfnInit;
	SetPalette          pfnSetPalette;
	FinishUpdate        pfnFinishUpdate;
	Draw2DLine          pfnDraw2DLine;
	DrawPolygon         pfnDrawPolygon;
	SetBlend            pfnSetBlend;
	ClearBuffer         pfnClearBuffer;
	SetTexture          pfnSetTexture;
	ReadRect            pfnReadRect;
	GClipRect           pfnGClipRect;
	ClearMipMapCache    pfnClearMipMapCache;
	SetSpecialState     pfnSetSpecialState;
	DrawModel           pfnDrawModel;
	CreateModelVBOs     pfnCreateModelVBOs;
	SetTransform        pfnSetTransform;
	GetTextureUsed      pfnGetTextureUsed;
	PostImgRedraw       pfnPostImgRedraw;
	FlushScreenTextures pfnFlushScreenTextures;
	StartScreenWipe     pfnStartScreenWipe;
	EndScreenWipe       pfnEndScreenWipe;
	DoScreenWipe        pfnDoScreenWipe;
	DrawIntermissionBG  pfnDrawIntermissionBG;
	MakeScreenTexture   pfnMakeScreenTexture;
	MakeScreenFinalTexture  pfnMakeScreenFinalTexture;
	DrawScreenFinalTexture  pfnDrawScreenFinalTexture;

	RenderSkyDome pfnRenderSkyDome;

	LoadShaders pfnLoadShaders;
	KillShaders pfnKillShaders;
	SetShader pfnSetShader;
	UnSetShader pfnUnSetShader;

	LoadCustomShader pfnLoadCustomShader;
	InitCustomShaders pfnInitCustomShaders;

	StartBatching pfnStartBatching;
	RenderBatches pfnRenderBatches;

	SetStereoMode pfnSetStereoMode;
	ReapplyStereoMode pfnReapplyStereoMode;
	ResetStereoMode pfnResetStereoMode;

	MakeScreenTextureSized pfnMakeScreenTextureSized;
	DrawInterlacedComposite pfnDrawInterlacedComposite;
};

extern struct hwdriver_s hwdriver;

#define HWD hwdriver

#endif //not defined _CREATE_DLL_

#endif //__HWR_DRV_H__