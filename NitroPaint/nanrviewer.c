#include <Windows.h>
#include <CommCtrl.h>
#include <math.h>

#include "nanrviewer.h"
#include "nclrviewer.h"
#include "ncgrviewer.h"
#include "ncerviewer.h"
#include "nitropaint.h"
#include "resource.h"
#include "childwindow.h"
#include "nclr.h"
#include "ncgr.h"
#include "ncer.h"
#include "preview.h"
#include "gdip.h"

extern HICON g_appIcon;

#define FX32_ONE               4096
#define FX32_HALF              (FX32_ONE/2)
#define FX32_FROM_F32(x)       ((int)(((x)<0.0f)?((x)*FX32_ONE+0.5f):((x)*FX32_ONE-0.5f)))

#define RAD_0DEG               0.00000000000000000
#define RAD_22_5DEG            0.39269908169872415
#define RAD_45DEG              0.78539816339744831
#define RAD_90DEG              1.57079632679489662
#define RAD_180DEG             3.14159265358979323
#define RAD_360DEG             6.28318530717958648

#define SEXT8(n) (((n)<0x080)?(n):((n)-0x100))
#define SEXT9(n) (((n)<0x100)?(n):((n)-0x200))

#define NANRVIEWER_TIMER_TICK       1

#define PREVIEW_ICON_WIDTH         64 // width of cell preview icon
#define PREVIEW_ICON_HEIGHT        64 // height of cell preview icon
#define PREVIEW_ICON_PADDING_V     10 // vertical padding of cell preview

#define ANM_HIT_TYPE_MASK      0x00FF
#define ANM_HIT_FLAG_MASK      0xFF00
#define ANM_HIT_NOWHERE             0 // hits nowhere
#define ANM_HIT_ANCHOR              1 // hits the anchor
#define ANM_HIT_BOX                 2 // hits the box
#define ANM_HIT_ROT_POINT           3 // rotation point
#define ANM_HIT_ROT_CIRCLE          4 // rotation circle
#define ANM_HIT_U               0x100
#define ANM_HIT_D               0x200
#define ANM_HIT_L               0x400
#define ANM_HIT_R               0x800

#define ANCHOR_SIZE                 6 // anchor point size
#define CELL_PADDING_SIZE           8 // cell padding size

// interpolation settings
typedef struct AnmViewerInterpolateSetting_ {
	//inputs:
	ANIM_DATA_SRT start;              // start transformation
	ANIM_DATA_SRT end;                // end transformation

	//UI inputs:
	unsigned int linear    : 1;       // interpolate linear transformation (rather than SRT)
	unsigned int clockwise : 1;       // interpolate angle clockwise (if not linear)
	unsigned int totalDuration;       // total duration of generated animation
	unsigned int nFrames;             // number of frames to generate

	//outputs:
	ANIM_DATA_SRT *result;            // resulting interpolated frames (excluding start+end)
	int *durations;                   // resulting frame durations
	int nResult;                      // number of interpolated frames
} AnmViewerInterpolateSetting;

static int AnmViewerPromptInterpolation(NANRVIEWERDATA *data, AnmViewerInterpolateSetting *setting);


static void AnmViewerStopPlayback(NANRVIEWERDATA *data);
static void AnmViewerStartPlayback(NANRVIEWERDATA *data);
static void AnmViewerTickPlayback(NANRVIEWERDATA *data);

static NANR_SEQUENCE *AnmViewerGetCurrentSequence(NANRVIEWERDATA *data);
static int AnmViewerGetCurrentAnimFrame(NANRVIEWERDATA *data, ANIM_DATA_SRT *pFrm, int *pDuration);
static int AnmViewerGetAnimFrame(NANRVIEWERDATA *data, int iSeq, int iFrm, ANIM_DATA_SRT *pFrm, int *pDuration);
static void AnmViewerRenderFrameFromCurrentSequence(NANRVIEWERDATA *data, COLOR32 *dest, int iFrm, BOOL fillBG);

static void AnmViewerPreviewGetScroll(NANRVIEWERDATA *data, int *pScrollX, int *pScrollY);

static int FloatToInt(double x) {
	return (int) (x + (x < 0.0f ? -0.5f : 0.5f));
}

//simple string to float, doesn't consider other weird string representations.
static float my_wtof(const wchar_t *str) {
	int intPart = _wtol(str), fracPart = 0, denominator = 1;

	while (*str && *str != L'.') {
		str++;
	}
	if (*str == L'.') {
		str++;
		fracPart = _wtol(str);

		int nDecimalDigits = wcslen(str);
		for (int i = 0; i < nDecimalDigits; i++) {
			denominator *= 10;
		}
	}

	if (intPart >= 0) {
		return ((float) intPart) + ((float) fracPart) / ((float) denominator);
	} else {
		return ((float) intPart) - ((float) fracPart) / ((float) denominator);
	}
}

static void FormatFxToString(WCHAR *buf, int fx) {
	if (fx < 0) {
		*(buf++) = L'-';
		fx = -fx;
	}
	int iPart = fx / 4096;
	int fPart = fx - 4096 * iPart;
	buf += wsprintfW(buf, L"%d", iPart);
	*(buf++) = L'.';

	for (int i = 0; i < 4; i++) {
		fPart *= 10;
		int digit = fPart / 4096;
		fPart %= 4096;
		*(buf++) = L'0' + digit;
	}
	*(buf++) = L'\0';
}

static void FormatAngleToString(WCHAR *buf, int fx) {
	fx &= 0xFFFF;
	double deg = ((double) fx) / 65536.0 * 360.0;

	int asInt = (int) (deg * 1000.0 + 0.5);
	buf += wsprintfW(buf, L"%d", asInt / 1000);
	*(buf++) = L'.';

	int dec1 = (asInt / 100) % 10;
	int dec2 = (asInt / 10) % 10;
	int dec3 = (asInt / 1) % 10;

	*(buf++) = L'0' + dec1;
	*(buf++) = L'0' + dec2;
	*(buf++) = L'0' + dec3;
	*(buf++) = L'\0';
}


// ----- linkage routines

static HWND AnmViewerGetAssociatedEditor(NANRVIEWERDATA *data, int type) {
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) data->editorMgr;
	switch (type) {
		case FILE_TYPE_PALETTE:
			return nitroPaintStruct->hWndNclrViewer;
		case FILE_TYPE_CHARACTER:
			return nitroPaintStruct->hWndNcgrViewer;
		case FILE_TYPE_CELL:
			return nitroPaintStruct->hWndNcerViewer;
	}
	return NULL;
}

static OBJECT_HEADER *AnmViewerGetAssociatedObject(NANRVIEWERDATA *data, int type) {
	HWND hWndEditor = AnmViewerGetAssociatedEditor(data, type);
	if (hWndEditor == NULL) return NULL;

	EDITOR_DATA *ed = EditorGetData(hWndEditor);
	if (ed == NULL) return NULL;

	return ed->file;
}

// ----- transformation calculation routines

static void AnmViewerInvMtx(double *pMtx) {
	double mtx[2][2];
	memcpy(mtx, pMtx, sizeof(mtx));

	double det = mtx[0][0] * mtx[1][1] - mtx[1][0] * mtx[0][1];
	if (det == 0.0f) return;

	pMtx[0 * 2 + 0] = mtx[1][1] / det;
	pMtx[0 * 2 + 1] = -mtx[0][1] / det;
	pMtx[1 * 2 + 0] = -mtx[1][0] / det;
	pMtx[1 * 2 + 1] = mtx[0][0] / det;
}

static void AnmViewerApplyScaleTransform(double cx, double cy, double *pTx, double *pTy, double *pSx, double *pSy, double rot, double newSx, double newSy) {
	double mtxNew[2][2], transNew[2], mtxOld[2][2], transOld[2];
	AnmCalcTransformMatrix(0.0f, 0.0f, newSx, newSy, rot, *pTx, *pTy, &mtxNew[0][0], &transNew[0]);
	AnmCalcTransformMatrix(0.0f, 0.0f, *pSx, *pSy, rot, *pTx, *pTy, &mtxOld[0][0], &transOld[0]);
	AnmViewerInvMtx(&mtxOld[0][0]);

	//set (tx, ty) such that it transforms the same by the inverses of both new and original.
	double cxAdj = cx - transOld[0];
	double cyAdj = cy - transOld[1];
	double cxAdj2 = (cxAdj * mtxOld[0][0] + cyAdj * mtxOld[0][1]);
	double cyAdj2 = (cxAdj * mtxOld[1][0] + cyAdj * mtxOld[1][1]);

	//transform by new matrix
	cxAdj = (cxAdj2 * mtxNew[0][0] + cyAdj2 * mtxNew[0][1]) + transNew[0];
	cyAdj = (cxAdj2 * mtxNew[1][0] + cyAdj2 * mtxNew[1][1]) + transNew[1];

	*pTx += cx - cxAdj;
	*pTy += cy - cyAdj;
	*pSx = newSx;
	*pSy = newSy;
}

static void AnmViewerApplyRotateTransform(double cx, double cy, double *pTx, double *pTy, double sx, double sy, double *pRot, double newRot) {
	double mtxNew[2][2], transNew[2], mtxOld[2][2], transOld[2];
	AnmCalcTransformMatrix(0.0f, 0.0f, sx, sy, newRot, *pTx, *pTy, &mtxNew[0][0], &transNew[0]);
	AnmCalcTransformMatrix(0.0f, 0.0f, sx, sy, *pRot, *pTx, *pTy, &mtxOld[0][0], &transOld[0]);
	AnmViewerInvMtx(&mtxOld[0][0]);

	//set (tx, ty) such that it transforms the same by the inverses of both new and original.
	double cxAdj = cx - transOld[0];
	double cyAdj = cy - transOld[1];
	double cxAdj2 = (cxAdj * mtxOld[0][0] + cyAdj * mtxOld[0][1]);
	double cyAdj2 = (cxAdj * mtxOld[1][0] + cyAdj * mtxOld[1][1]);

	//transform by new matrix
	cxAdj = (cxAdj2 * mtxNew[0][0] + cyAdj2 * mtxNew[0][1]) + transNew[0];
	cyAdj = (cxAdj2 * mtxNew[1][0] + cyAdj2 * mtxNew[1][1]) + transNew[1];

	*pTx += cx - cxAdj;
	*pTy += cy - cyAdj;
	*pRot = newRot;
}

static void AnmViewerTransform(int *pX, int *pY, double a, double b, double c, double d) {
	double x = (double) *pX;
	double y = (double) *pY;

	double x2 = (x * a + y * b);
	double y2 = (x * c + y * d);

	*pX = FloatToInt(x2);
	*pY = FloatToInt(y2);
}

static void AnmViewerEncodeTransform(ANIM_DATA_SRT *dst, const AnmTransSrt *src) {
	dst->px = FloatToInt(src->tx);
	dst->py = FloatToInt(src->ty);
	dst->sx = FloatToInt(src->sx * 4096.0);
	dst->sy = FloatToInt(src->sy * 4096.0);

	double rot = src->rot * 65536.0 / RAD_360DEG;
	dst->rotZ = FloatToInt(rot) & 0xFFFF;
}

static void AnmViewerDecodeTransform(AnmTransSrt *dst, const ANIM_DATA_SRT *src) {
	dst->tx = (double) src->px;
	dst->ty = (double) src->py;
	dst->sx = src->sx / 4096.0;
	dst->sy = src->sy / 4096.0;
	dst->rot = src->rotZ / 65536.0 * RAD_360DEG;
}

static void AnmViewerGetCurrentFrameTransform(NANRVIEWERDATA *data, double *pMtx, double *pTrans) {
	pMtx[0] = 1.0f;
	pMtx[1] = 0.0f;
	pMtx[2] = 0.0f;
	pMtx[3] = 1.0f;
	pTrans[0] = 0.0f;
	pTrans[1] = 0.0f;

	NCER *ncer = (NCER *) AnmViewerGetAssociatedObject(data, FILE_TYPE_CELL);
	if (ncer == NULL) return;

	ANIM_DATA_SRT frm;
	if (!AnmViewerGetCurrentAnimFrame(data, &frm, NULL)) return;

	AnmTransSrt srt;
	AnmViewerDecodeTransform(&srt, &frm);
	AnmCalcTransformMatrix(0.0f, 0.0f, srt.sx, srt.sy, srt.rot, srt.tx, srt.ty, pMtx, pTrans);
}


// ----- rendering helper routines

static void AnmViewerGetCellBoundCorners(int *ptUL, int *ptUR, int *ptDL, int *ptDR, int x, int y, int w, int h, double a, double b, double c, double d, int tx, int ty) {
	//get transformed coordinates
	ptUL[0] = x; ptUL[1] = y;
	ptUR[0] = x + w; ptUR[1] = y;
	ptDL[0] = x; ptDL[1] = y + h;
	ptDR[0] = x + w; ptDR[1] = y + h;

	//transform
	AnmViewerTransform(&ptUL[0], &ptUL[1], a, b, c, d); ptUL[0] += tx; ptUL[1] += ty;
	AnmViewerTransform(&ptUR[0], &ptUR[1], a, b, c, d); ptUR[0] += tx; ptUR[1] += ty;
	AnmViewerTransform(&ptDL[0], &ptDL[1], a, b, c, d); ptDL[0] += tx; ptDL[1] += ty;
	AnmViewerTransform(&ptDR[0], &ptDR[1], a, b, c, d); ptDR[0] += tx; ptDR[1] += ty;
}

static void AnmViewerDrawBoxRot(FrameBuffer *fb, const COLOR32 *cols, int x, int y, int w, int h, int cx, int cy, double a, double b, double c, double d) {
	//transform
	int ptUL[2], ptUR[2], ptDL[2], ptDR[2];
	AnmViewerGetCellBoundCorners(&ptUL[0], &ptUR[0], &ptDL[0], &ptDR[0], x, y, w, h, a, b, c, d, cx, cy);

	//draw lines
	FbDrawLine(fb, cols[0], ptUL[0], ptUL[1], ptUR[0], ptUR[1]);
	FbDrawLine(fb, cols[3], ptUR[0], ptUR[1], ptDR[0], ptDR[1]);
	FbDrawLine(fb, cols[1], ptDR[0], ptDR[1], ptDL[0], ptDL[1]);
	FbDrawLine(fb, cols[2], ptDL[0], ptDL[1], ptUL[0], ptUL[1]);
}

static void AnmViewerGetCellBounds(NCER_CELL *cell, int *pBoundX, int *pBoundY, int *pBoundW, int *pBoundH) {
	if (cell == NULL) return;

	int xMin = 0, xMax = 0, yMin = 0, yMax = 0;

	for (int i = 0; i < cell->nAttribs; i++) {
		NCER_CELL_INFO info;
		CellDecodeOamAttributes(&info, cell, i);

		int objX = SEXT9(info.x), objY = SEXT8(info.y);
		int objW = info.width << info.doubleSize, objH = info.height << info.doubleSize;

		//when OBJ is double size, we'll restrict the OBJ to the un-transformed portion.
		if (info.doubleSize) {
			objX += objW / 4;
			objY += objH / 4;
			objW /= 2;
			objH /= 2;
		}

		if (i == 0 || objX < xMin) xMin = objX;
		if (i == 0 || objY < yMin) yMin = objY;
		if (i == 0 || (objX + objW) > xMax) xMax = objX + objW;
		if (i == 0 || (objY + objH) > yMax) yMax = objY + objH;
	}

	*pBoundX = xMin;
	*pBoundY = yMin;
	*pBoundW = xMax - xMin;
	*pBoundH = yMax - yMin;
}


// ----- hit testing routines

static double AnmViewerGetPtAngle(NANRVIEWERDATA *data, double rotX, double rotY) {
	//rotation point
	double rotD = sqrt(rotX * rotX + rotY * rotY);
	if (rotD != 0.0f) {
		rotX /= rotD;
		rotY /= rotD;
	}

	double newrot;
	if (rotY <= 0.0) {
		newrot = -acos(rotX) + RAD_180DEG;
	} else {
		newrot = -acos(-rotX);
	}
	newrot -= RAD_90DEG;
	return newrot;
}

static void AnmViewerGetRotCircle(NANRVIEWERDATA *data, int *pX, int *pY, int *pR) {
	if (!data->mouseDown || !(data->mouseDownHit == ANM_HIT_ROT_POINT || data->mouseDownHit == ANM_HIT_ROT_CIRCLE)) {
		//mouse not down: calculate circle size
		int px = data->rotPtX * data->scale;
		int py = data->rotPtY * data->scale;
		int cx = data->anchorX * data->scale;
		int cy = data->anchorY * data->scale;
		int r = (int) (sqrt((cx - px) * (cx - px) + (cy - py) * (cy - py)) + 0.5f);

		//set computed circle size
		data->mouseDownCircleX = cx;
		data->mouseDownCircleY = cy;
		data->mouseDownCircleR = r;
	} else {
		//mouse down: use pre-computed circle (to stabilize the circle size from roundoff)
	}
	*pX = data->mouseDownCircleX;
	*pY = data->mouseDownCircleY;
	*pR = data->mouseDownCircleR;
}

static int AnmViewerHitTest(NANRVIEWERDATA *data, int clientX, int clientY) {
	int scrollX, scrollY;
	AnmViewerPreviewGetScroll(data, &scrollX, &scrollY);

	int x = clientX + scrollX;
	int y = clientY + scrollY;

	//check anchor position
	int anchorX = (data->anchorX + 256) * data->scale - ANCHOR_SIZE / 2;
	int anchorY = (data->anchorY + 128) * data->scale - ANCHOR_SIZE / 2;
	if (x >= anchorX && y >= anchorY && x < (anchorX + ANCHOR_SIZE) && y < (anchorY + ANCHOR_SIZE)) return ANM_HIT_ANCHOR;

	//rotation point
	int rotX = (data->rotPtX + 256) * data->scale - ANCHOR_SIZE / 2;
	int rotY = (data->rotPtY + 128) * data->scale - ANCHOR_SIZE / 2;
	if (x >= rotX && y >= rotY && x < (rotX + ANCHOR_SIZE) && y < (rotY + ANCHOR_SIZE)) return ANM_HIT_ROT_POINT;

	//get cell data
	NCER *ncer = (NCER *) AnmViewerGetAssociatedObject(data, FILE_TYPE_CELL);
	if (ncer == NULL) return ANM_HIT_NOWHERE;

	ANIM_DATA_SRT frm;
	if (!AnmViewerGetCurrentAnimFrame(data, &frm, NULL)) return ANM_HIT_NOWHERE;
	if (frm.index < 0 || frm.index >= ncer->nCells) return ANM_HIT_NOWHERE;

	//cell bounds
	NCER_CELL *cell = &ncer->cells[frm.index];
	int cellX, cellY, cellW, cellH;
	AnmViewerGetCellBounds(cell, &cellX, &cellY, &cellW, &cellH);

	//get transform
	double mtx[2][2], trans[2];
	AnmViewerGetCurrentFrameTransform(data, &mtx[0][0], &trans[0]);

	//effective rectangle: we'll scale it up to the viewe's size.
	cellX *= data->scale; cellY *= data->scale; cellW *= data->scale; cellH *= data->scale;
	trans[0] *= data->scale;
	trans[1] *= data->scale;

	//add padding size to cell bound
	cellX -= CELL_PADDING_SIZE / 2;
	cellY -= CELL_PADDING_SIZE / 2;
	cellW += CELL_PADDING_SIZE;
	cellH += CELL_PADDING_SIZE;

	//tranform cursor position by the inverse matrix
	double invMtx[2][2];
	memcpy(invMtx, mtx, sizeof(mtx));
	AnmViewerInvMtx(&invMtx[0][0]);

	int effectiveX = x - 256 * data->scale - FloatToInt(trans[0]);
	int effectiveY = y - 128 * data->scale - FloatToInt(trans[1]);
	AnmViewerTransform(&effectiveX, &effectiveY, invMtx[0][0], invMtx[0][1], invMtx[1][0], invMtx[1][1]);

	if (effectiveX >= cellX && effectiveY >= cellY && effectiveX < (cellX + cellW) && effectiveY < (cellY + cellH)) {
		//hit box
		int flg = 0;

		if (effectiveX < (cellX + CELL_PADDING_SIZE)) {
			flg |= ANM_HIT_L;
		} else if (effectiveX >= (cellX + cellW - CELL_PADDING_SIZE)) {
			flg |= ANM_HIT_R;
		}

		if (effectiveY < (cellY + CELL_PADDING_SIZE)) {
			flg |= ANM_HIT_U;
		} else if (effectiveY >= (cellY + cellH - CELL_PADDING_SIZE)) {
			flg |= ANM_HIT_D;
		}

		return ANM_HIT_BOX | flg;
	}

	//rotation circle
	int rotCX, rotCY, rotCR;
	AnmViewerGetRotCircle(data, &rotCX, &rotCY, &rotCR);
	{
		int cursorRotX = x - 256 * data->scale - rotCX;
		int cursorRotY = y - 128 * data->scale - rotCY;
		int cursorRotR2 = cursorRotX * cursorRotX + cursorRotY * cursorRotY;
		int rMax = rotCR + CELL_PADDING_SIZE / 2;
		int rMin = rotCR - CELL_PADDING_SIZE / 2;
		if (rMin < 0) rMin = 0;

		if (cursorRotR2 >= (rMin * rMin) && cursorRotR2 <= (rMax * rMax)) {
			return ANM_HIT_ROT_CIRCLE;
		}
	}

	return ANM_HIT_NOWHERE;
}

static int AnmViewerGetEffectiveHit(NANRVIEWERDATA *data) {
	if (!data->mouseDown) return data->hit;
	return data->mouseDownHit;
}

static HCURSOR AnmViewerGetArrowCursorForAngle(NANRVIEWERDATA *data, double angle) {
	//in rotations, adjusted by 22.5 degrees, scaled up to octants
	double adj = (angle / RAD_360DEG) * 8.0;
	int octant = FloatToInt(adj);
	octant &= 0x7;

	switch (octant) {
		case 0: return LoadCursor(NULL, IDC_SIZENS);   // -22.5 to  22.5
		case 1: return LoadCursor(NULL, IDC_SIZENESW); //  22.5 to  67.5
		case 2: return LoadCursor(NULL, IDC_SIZEWE);   //  67.5 to 112.5
		case 3: return LoadCursor(NULL, IDC_SIZENWSE); // 112.5 to 157.5
		case 4: return LoadCursor(NULL, IDC_SIZENS);   // 157.5 to 202.5
		case 5: return LoadCursor(NULL, IDC_SIZENESW); // 202.5 to 247.5
		case 6: return LoadCursor(NULL, IDC_SIZEWE);   // 247.5 to 292.5
		case 7: return LoadCursor(NULL, IDC_SIZENWSE); // 292.5 to 337.5
	}

	return LoadCursor(NULL, IDC_SIZENS);
}

static HCURSOR AnmViewerHitCursorRotCircle(NANRVIEWERDATA *data) {
	int scrollX, scrollY;
	POINT ptCursor;
	GetCursorPos(&ptCursor);
	ScreenToClient(data->hWndPreview, &ptCursor);

	AnmViewerPreviewGetScroll(data, &scrollX, &scrollY);
	
	double rotX = (double) (ptCursor.x + scrollX - 256 * data->scale - data->anchorX * data->scale);
	double rotY = (double) (ptCursor.y + scrollY - 128 * data->scale - data->anchorY * data->scale);
	double newrot = AnmViewerGetPtAngle(data, rotX, rotY);

	return AnmViewerGetArrowCursorForAngle(data, newrot + RAD_90DEG);
}

static HCURSOR AnmViewerHitCursorBox(NANRVIEWERDATA *data, int hit) {
	//hit box
	int flag = hit & ANM_HIT_FLAG_MASK;
	if (flag == 0) return LoadCursor(NULL, IDC_SIZEALL);

	//get matrix transform to map edge hits to sizing cursors
	double mtx[2][2], trans[2];
	AnmViewerGetCurrentFrameTransform(data, &mtx[0][0], &trans[0]);

	double upX = mtx[0][1], upY = mtx[1][1];
	double upMag = sqrt(upX * upX + upY * upY);
	if (upMag > 0.0f) {
		upX /= upMag;
		upY /= upMag;
	}
	if (upX < 0.0f) {
		//invert into Qi or Qiv
		upX = -upX;
		upY = -upY;
	}

	double baseAngle = acos(-upY);
	baseAngle = baseAngle / RAD_360DEG; // to rotations

	//hit edges
	switch (flag) {
		case ANM_HIT_U:
		case ANM_HIT_D:
			baseAngle += 0.000;
			break;
		case ANM_HIT_L:
		case ANM_HIT_R:
			baseAngle += 0.250;
			break;
		case ANM_HIT_U | ANM_HIT_L:
		case ANM_HIT_D | ANM_HIT_R:
			baseAngle += 0.375;
			break;
		case ANM_HIT_U | ANM_HIT_R:
		case ANM_HIT_D | ANM_HIT_L:
			baseAngle += 0.125;
			break;
	}

	//clamp
	return AnmViewerGetArrowCursorForAngle(data, baseAngle * RAD_360DEG);
}

static HCURSOR AnmViewerGetHitCursor(NANRVIEWERDATA *data, int hit) {
	switch (hit & ANM_HIT_TYPE_MASK) {
		case ANM_HIT_NOWHERE:
			return LoadCursor(NULL, IDC_ARROW);
		case ANM_HIT_ANCHOR:
			return LoadCursor(NULL, IDC_CROSS);
		case ANM_HIT_ROT_POINT:
			return LoadCursor(NULL, IDC_CROSS);
		case ANM_HIT_ROT_CIRCLE:
			return AnmViewerHitCursorRotCircle(data);
		case ANM_HIT_BOX:
			return AnmViewerHitCursorBox(data, hit);
		default:
			return LoadCursor(NULL, IDC_ARROW);
	}
}


// ----- animation list routines

static void AnmViewerFrameListSelectFrame(NANRVIEWERDATA *data, int i) {
	if (data->hWndFrameList == NULL) return;

	if (i != -1) {
		ListView_SetItemState(data->hWndFrameList, -1, 0, LVIS_SELECTED);
		ListView_SetItemState(data->hWndFrameList, i, LVIS_SELECTED, LVIS_SELECTED);
	}
}

static void AnmViewerFrameListUpdate(NANRVIEWERDATA *data) {
	if (data->hWndFrameList != NULL) {
		NANR_SEQUENCE *seq = AnmViewerGetCurrentSequence(data);
		int nFramesSeq = 0;
		if (seq != NULL) {
			nFramesSeq = seq->nFrames;
		}
		if (ListView_GetItemCount(data->hWndFrameList) != nFramesSeq) {
			ListView_SetItemCount(data->hWndFrameList, nFramesSeq);
		}

		InvalidateRect(data->hWndFrameList, NULL, FALSE);
	}
}

static HWND AnmViewerAnimListCreate(NANRVIEWERDATA *data) {
	HWND hWndParent = data->hWnd;
	float scale = GetDpiScale();
	int listWidth = UI_SCALE_COORD(200, scale);

	DWORD lvStyle = WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPCHILDREN | WS_VSCROLL | LVS_EDITLABELS | LVS_SINGLESEL | LVS_ICON | LVS_SHOWSELALWAYS
		| LVS_OWNERDATA;
	HWND h = CreateWindow(WC_LISTVIEW, L"", lvStyle, 0, 0, listWidth, 300, hWndParent, NULL, NULL, NULL);

	//set extended style
	ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_JUSTIFYCOLUMNS | LVS_EX_SNAPTOGRID);
	SendMessage(h, LVM_SETVIEW, LV_VIEW_TILE, 0);

	RECT rcClient;
	GetClientRect(h, &rcClient);

	//set tile view info
	LVTILEVIEWINFO lvtvi = { 0 };
	lvtvi.cbSize = sizeof(lvtvi);
	lvtvi.dwMask = LVTVIM_COLUMNS | LVTVIM_TILESIZE | LVTVIM_COLUMNS;
	lvtvi.dwFlags = LVTVIF_FIXEDSIZE;
	lvtvi.cLines = 1;
	lvtvi.sizeTile.cx = rcClient.right - GetSystemMetrics(SM_CXVSCROLL);
	lvtvi.sizeTile.cy = PREVIEW_ICON_HEIGHT + PREVIEW_ICON_PADDING_V;
	ListView_SetTileViewInfo(h, &lvtvi);

	//init columns
	LVCOLUMN lvc = { 0 };
	lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM;
	lvc.iSubItem = 0;
	lvc.pszText = L"";
	lvc.fmt = LVCFMT_LEFT;
	ListView_InsertColumn(h, 0, &lvc);
	lvc.iSubItem = 1;
	ListView_InsertColumn(h, 1, &lvc);

	//create image list
	HIMAGELIST hLarge = ImageList_Create(PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, ILC_MASK | ILC_COLOR24, 1, 1);
	ListView_SetImageList(h, hLarge, LVSIL_NORMAL);

	//SetWindowSubclass(h, CellViewerCellListSubclassProc, 1, 0);

	return h;
}

static void AnmViewerRenderGlyphListImage(NANRVIEWERDATA *data, int i) {
	HIMAGELIST himl = ListView_GetImageList(data->hWndAnimList, LVSIL_NORMAL);

	//get data
	NCLR *nclr = (NCLR *) AnmViewerGetAssociatedObject(data, FILE_TYPE_PALETTE);
	NCGR *ncgr = (NCGR *) AnmViewerGetAssociatedObject(data, FILE_TYPE_CHARACTER);
	NCER *ncer = (NCER *) AnmViewerGetAssociatedObject(data, FILE_TYPE_CELL);

	//get frame data
	ANIM_DATA_SRT frm;
	int frmExist = AnmViewerGetAnimFrame(data, i, 0, &frm, NULL);

	//get referenced cell
	NCER_CELL *cell = NULL;
	if (frmExist && ncer != NULL && frm.index < ncer->nCells) {
		cell = &ncer->cells[frm.index];
	}

	//render
	memset(data->cellRender, 0, sizeof(data->cellRender));
	if (cell != NULL) {
		double mtx[2][2], trans[2];

		double sx = frm.sx / 4096.0;
		double sy = frm.sy / 4096.0;
		double rot = (frm.rotZ / 65536.0) * RAD_360DEG;

		AnmCalcTransformMatrix(0.0f, 0.0f, sx, sy, rot, (double) frm.px, (double) frm.py, &mtx[0][0], trans);
		CellRender(data->cellRender, NULL, ncer, ncgr, nclr, frm.index, cell,
			FloatToInt(trans[0]), FloatToInt(trans[1]),
			mtx[0][0], mtx[0][1], mtx[1][0], mtx[1][1],
			data->forceAffine, data->forceDoubleSize);
	}

	//next, crop the rendered cell
	int minX, minY, cropW, cropH;
	COLOR32 *crop = CellViewerCropRenderedCell(data->cellRender, 512, 256, &minX, &minY, &cropW, &cropH);

	//produce scaled+cropped image
	COLOR32 *scaled = ImgScaleEx(crop, cropW, cropH, PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, IMG_SCALE_FIT);
	free(crop);

	//render mask
	unsigned char *mask = ImgCreateAlphaMask(scaled, PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, 0x80, NULL, NULL);

	//create bitmaps
	HBITMAP hBmColor = CreateBitmap(PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, 1, 32, scaled);
	HBITMAP hBmAlpha = CreateBitmap(PREVIEW_ICON_WIDTH, PREVIEW_ICON_HEIGHT, 1, 1, mask);
	free(scaled);
	free(mask);

	ImageList_Replace(himl, 0, hBmColor, hBmAlpha);
	DeleteObject(hBmColor);
	DeleteObject(hBmAlpha);
}

static void AnmViewerSetRotationPoint(NANRVIEWERDATA *data) {
	NCER *ncer = (NCER *) AnmViewerGetAssociatedObject(data, FILE_TYPE_CELL);
	if (ncer == NULL) return;

	ANIM_DATA_SRT frame;
	if (!AnmViewerGetAnimFrame(data, data->currentAnim, data->currentFrame, &frame, NULL)) return;

	int cellIndex = frame.index;
	if (cellIndex < 0 || cellIndex >= ncer->nCells) return;
	NCER_CELL *cell = &ncer->cells[cellIndex];

	//get cell bounds
	int xMin, yMin, w, h;
	AnmViewerGetCellBounds(cell, &xMin, &yMin, &w, &h);

	double mtx[2][2], trans[2];
	AnmViewerGetCurrentFrameTransform(data, &mtx[0][0], &trans[0]);

	//get the bounding coordinates
	int ptUL[2], ptUR[2], ptDL[2], ptDR[2];
	AnmViewerGetCellBoundCorners(&ptUL[0], &ptUR[0], &ptDL[0], &ptDR[0], xMin, yMin, w, h,
		mtx[0][0], mtx[0][1], mtx[1][0], mtx[1][1], FloatToInt(trans[0]), FloatToInt(trans[1]));

	int cx = data->anchorX, cy = data->anchorY;
	int d2UL = (ptUL[0] - cx) * (ptUL[0] - cx) + (ptUL[1] - cy) * (ptUL[1] - cy);
	int d2UR = (ptUR[0] - cx) * (ptUR[0] - cx) + (ptUR[1] - cy) * (ptUR[1] - cy);
	int d2DL = (ptDL[0] - cx) * (ptDL[0] - cx) + (ptDL[1] - cy) * (ptDL[1] - cy);
	int d2DR = (ptDR[0] - cx) * (ptDR[0] - cx) + (ptDR[1] - cy) * (ptDR[1] - cy);

	//max distance
	int maxD2 = d2UL;
	if (d2UR > maxD2) maxD2 = d2UR;
	if (d2DL > maxD2) maxD2 = d2DL;
	if (d2DR > maxD2) maxD2 = d2DR;
	double maxD = sqrt((double) maxD2);

	//rotate and add displacement
	double rot = frame.rotZ / 65536.0 * RAD_360DEG;
	double rotX = cos(rot - RAD_90DEG) * maxD + (double) cx;
	double rotY = sin(rot - RAD_90DEG) * maxD + (double) cy;
	data->rotPtX = FloatToInt(rotX);
	data->rotPtY = FloatToInt(rotY);
}

static void AnmViewerSetAnchor(NANRVIEWERDATA *data, int x, int y) {
	//set anchor
	data->anchorX = x;
	data->anchorY = y;

	//set rotation point
	AnmViewerSetRotationPoint(data);
}

static void AnmViewerSetDefaultAnchor(NANRVIEWERDATA *data) {
	NCER *ncer = (NCER *) AnmViewerGetAssociatedObject(data, FILE_TYPE_CELL);
	if (ncer == NULL) return;

	ANIM_DATA_SRT frame;
	if (!AnmViewerGetAnimFrame(data, data->currentAnim, data->currentFrame, &frame, NULL)) return;

	int cellIndex = frame.index;
	if (cellIndex < 0 || cellIndex >= ncer->nCells) return;
	NCER_CELL *cell = &ncer->cells[cellIndex];

	//update default anchor position
	int xMin, yMin, w, h;
	AnmViewerGetCellBounds(cell, &xMin, &yMin, &w, &h);

	double mtx[2][2], trans[2];
	AnmViewerGetCurrentFrameTransform(data, &mtx[0][0], &trans[0]);

	int anchorX = xMin + w / 2;
	int anchorY = yMin + h / 2;
	AnmViewerTransform(&anchorX, &anchorY, mtx[0][0], mtx[0][1], mtx[1][0], mtx[1][1]);

	//set anchor
	anchorX += FloatToInt(trans[0]);
	anchorY += FloatToInt(trans[1]);
	AnmViewerSetAnchor(data, anchorX, anchorY);
}

static void AnmViewerSetCurrentFrame(NANRVIEWERDATA *data, int frm, BOOL updateList) {
	NANR_SEQUENCE *seq = AnmViewerGetCurrentSequence(data);
	if (seq == NULL) return;
	if (frm < 0 || frm >= seq->nFrames) return;

	data->currentFrame = frm;
	InvalidateRect(data->hWndPreview, NULL, FALSE);

	if (updateList) {
		AnmViewerFrameListSelectFrame(data, frm);
	}

	AnmViewerSetDefaultAnchor(data);
}

static void AnmViewerSetCurrentSequence(NANRVIEWERDATA *data, int seq, BOOL updateList) {
	if (seq < 0 || seq >= data->nanr->nSequences || seq == data->currentAnim) return;

	//update list
	if (updateList) {
		ListView_SetItemState(data->hWndAnimList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		if (seq != -1) ListView_SetItemState(data->hWndAnimList, seq, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}

	//set item count of frame list
	if (data->hWndFrameList != NULL) {
		ListView_SetItemCount(data->hWndFrameList, data->nanr->sequences[seq].nFrames);
		InvalidateRect(data->hWndFrameList, NULL, FALSE);
	}

	//set frame type
	UiCbSetCurSel(data->hWndPlayMode, data->nanr->sequences[seq].mode - NANR_SEQ_MODE_FORWARD);

	//set state
	data->currentAnim = seq;
	AnmViewerSetCurrentFrame(data, 0, TRUE);
	AnmViewerStopPlayback(data);
	InvalidateRect(data->hWndPreview, NULL, FALSE);
}


// ----- core routines

static NANR_SEQUENCE *AnmViewerGetCurrentSequence(NANRVIEWERDATA *data) {
	if (data->currentAnim < 0 || data->currentAnim >= data->nanr->nSequences) return NULL;
	return &data->nanr->sequences[data->currentAnim];
}

static int AnmViewerGetAnimFrame(NANRVIEWERDATA *data, int iSeq, int iFrm, ANIM_DATA_SRT *pFrame, int *pDuration) {
	return AnmGetAnimFrame(data->nanr, iSeq, iFrm, pFrame, pDuration);
}

static int AnmViewerGetCurrentAnimFrame(NANRVIEWERDATA *data, ANIM_DATA_SRT *pFrame, int *pDuration) {
	return AnmViewerGetAnimFrame(data, data->currentAnim, data->currentFrame, pFrame, pDuration);
}

static void AnmViewerPutCurrentAnimFrame(NANRVIEWERDATA *data, const ANIM_DATA_SRT *frm, const int *pDuration) {
	NANR_SEQUENCE *seq = AnmViewerGetCurrentSequence(data);
	if (seq == NULL) return;
	if (data->currentFrame < 0 || data->currentFrame >= seq->nFrames) return;

	FRAME_DATA *frameData = &seq->frames[data->currentFrame];
	if (pDuration != NULL) frameData->nFrames = *pDuration;

	if (frm != NULL) {
		switch (seq->type & 0xFFFF) {
			case NANR_SEQ_TYPE_INDEX:
			{
				ANIM_DATA *dat = (ANIM_DATA *) frameData->animationData;
				dat->index = frm->index;
				break;
			}
			case NANR_SEQ_TYPE_INDEX_T:
			{
				ANIM_DATA_T *dat = (ANIM_DATA_T *) frameData->animationData;
				dat->index = frm->index;
				dat->px = frm->px;
				dat->py = frm->py;
				dat->pad_ = 0xBEEF;
				break;
			}
			case NANR_SEQ_TYPE_INDEX_SRT:
			{
				ANIM_DATA_SRT *dat = (ANIM_DATA_SRT *) frameData->animationData;
				memcpy(dat, frm, sizeof(ANIM_DATA_SRT));
				break;
			}
		}
	}
	AnmViewerFrameListUpdate(data);
}

static void AnmViewerStartPlayback(NANRVIEWERDATA *data) {
	if (data->resetFlag) {
		NANR_SEQUENCE *seq = AnmViewerGetCurrentSequence(data);
		int initFrame = 0;
		if (seq != NULL) initFrame = seq->startFrameIndex;

		data->curFrameTime = 0;         // start frame
		data->direction = 0;            // forward
		data->currentFrame = initFrame; // initial frame
	}

	data->resetFlag = 0; // clear re-set flag

	//start timer
	data->playing = 1;
	SetTimer(data->hWnd, NANRVIEWER_TIMER_TICK, 16, NULL);

	SendMessage(data->hWndPlayPause, WM_SETTEXT, 0, (LPARAM) L"Pause");
	SendMessage(data->hWndStop, WM_SETTEXT, 0, (LPARAM) L"Stop");

}

static void AnmViewerPausePlayback(NANRVIEWERDATA *data) {
	data->playing = 0;

	//kill timer
	KillTimer(data->hWnd, NANRVIEWER_TIMER_TICK);

	//update button text
	SendMessage(data->hWndPlayPause, WM_SETTEXT, 0, (LPARAM) L"Play");
	SendMessage(data->hWndStop, WM_SETTEXT, 0, (LPARAM) L"Step");
}

static void AnmViewerStopPlayback(NANRVIEWERDATA *data) {
	AnmViewerPausePlayback(data);
	data->resetFlag = 1; // set the reset flag

	//set anchor
	AnmViewerSetDefaultAnchor(data);
}

static void AnmViewerAdvanceSequence(NANRVIEWERDATA *data) {
	//start next frame
	data->curFrameTime = 0;
	data->resetFlag = 0;

	NANR_SEQUENCE *seq = AnmViewerGetCurrentSequence(data);

	if (!data->direction) {
		//forwards
		data->currentFrame++;

		//bounds checks, looping
		if (data->currentFrame >= seq->nFrames) {
			data->currentFrame--;

			//control looping
			switch (seq->mode) {
				case NANR_SEQ_MODE_FORWARD:
					//forward (no loop): end playback
					AnmViewerStopPlayback(data);
					break;
				case NANR_SEQ_MODE_FORWARD_LOOP:
					//forward loop: restart playback
					data->currentFrame = 0; // start frame
					break;
				case NANR_SEQ_MODE_BACKWARD:
				case NANR_SEQ_MODE_BACKWARD_LOOP:
					//backward (with or without loop): reverse direction
					data->direction = 1;
					if (data->currentFrame > 0) data->currentFrame--;
					break;
			}
		}
	} else {
		//backwards
		data->currentFrame--;

		//bounds checks, looping
		if (data->currentFrame < 0) {
			data->currentFrame = 0;

			//control looping
			switch (seq->mode) {
				case NANR_SEQ_MODE_FORWARD:
				case NANR_SEQ_MODE_FORWARD_LOOP:
					//forward (with or without loop): how did we get here?
					AnmViewerStopPlayback(data);
					break;
				case NANR_SEQ_MODE_BACKWARD:
					//backward (no loop): stop playback
					AnmViewerStopPlayback(data);
					break;
				case NANR_SEQ_MODE_BACKWARD_LOOP:
					//backward loop: restart
					data->direction = 0;
					data->currentFrame = 1;
					break;
			}
		}
	}

	if (data->currentFrame >= seq->nFrames) data->currentFrame = seq->nFrames - 1;
	if (data->currentFrame < 0) data->currentFrame = 0;
	AnmViewerFrameListSelectFrame(data, data->currentFrame);
}

static void AnmViewerTickPlayback(NANRVIEWERDATA *data) {
	if (!data->playing) return;

	//increment frame time
	data->curFrameTime++;
	
	//compare to frame duration
	int duration;
	if (!AnmViewerGetCurrentAnimFrame(data, NULL, &duration)) return;

	//if frame time exceeds this frame's duration, advance the sequence
	if (data->curFrameTime >= duration) {
		AnmViewerAdvanceSequence(data);
	}
}

static void AnmViewerDeleteCurrentFrame(NANRVIEWERDATA *data) {
	NANR_SEQUENCE *seq = AnmViewerGetCurrentSequence(data);
	if (seq == NULL) return;

	int frm = data->currentFrame;
	if (frm < 0 || frm >= seq->nFrames) return;

	//remove frame
	free(seq->frames[frm].animationData);
	memmove(&seq->frames[frm], &seq->frames[frm + 1], (seq->nFrames - frm - 1) * sizeof(FRAME_DATA));
	seq->nFrames--;

	//adjust current frame
	if (frm >= seq->nFrames) {
		frm--;
		if (frm < 0) frm = 0;
	}
	data->currentFrame = frm;

	AnmViewerFrameListSelectFrame(data, frm);
	AnmViewerFrameListUpdate(data);
	InvalidateRect(data->hWndPreview, NULL, FALSE);
}

static void AnmViewerDeleteCurrentSequence(NANRVIEWERDATA *data) {
	NANR_SEQUENCE *seq = AnmViewerGetCurrentSequence(data);
	if (seq == NULL) return;

	//free the sequence data
	for (int i = 0; i < seq->nFrames; i++) {
		FRAME_DATA *frm = &seq->frames[i];
		free(frm->animationData);
	}
	free(seq->frames);

	//remove
	int i = data->currentAnim;
	memmove(&data->nanr->sequences[i], &data->nanr->sequences[i + 1], (data->nanr->nSequences - 1 - i) * sizeof(NANR_SEQUENCE));
	data->nanr->nSequences--;
	data->nanr->sequences = (NANR_SEQUENCE *) realloc(data->nanr->sequences, data->nanr->nSequences * sizeof(NANR_SEQUENCE));
	ListView_SetItemCount(data->hWndAnimList, data->nanr->nSequences);

	//update current
	if (i >= data->nanr->nSequences) i--;
	if (i < 0) i++;
	AnmViewerSetCurrentSequence(data, i, TRUE);
	AnmViewerSetDefaultAnchor(data);
	InvalidateRect(data->hWndAnimList, NULL, FALSE);
	InvalidateRect(data->hWndPreview, NULL, FALSE);
}

static void AnmViewerPreviewGetScroll(NANRVIEWERDATA *data, int *scrollX, int *scrollY) {
	//get scroll info
	SCROLLINFO scrollH = { 0 }, scrollV = { 0 };
	scrollH.cbSize = scrollV.cbSize = sizeof(scrollH);
	scrollH.fMask = scrollV.fMask = SIF_ALL;
	GetScrollInfo(data->hWndPreview, SB_HORZ, &scrollH);
	GetScrollInfo(data->hWndPreview, SB_VERT, &scrollV);

	*scrollX = scrollH.nPos;
	*scrollY = scrollV.nPos;
}



// ----- window procedure

static void AnmViewerCmdOnPlayPause(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) param;

	//invert playback state
	if (data->playing) {
		AnmViewerPausePlayback(data);
		AnmViewerSetDefaultAnchor(data);
	} else {
		AnmViewerStartPlayback(data);
	}
	InvalidateRect(data->hWndPreview, NULL, FALSE);
}

static void AnmViewerCmdOnStop(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) param;

	//if playing, this is the stop button. If not playing, this is the step button.
	if (data->playing) {
		AnmViewerStopPlayback(data);
	} else {
		AnmViewerAdvanceSequence(data);
	}
	AnmViewerSetDefaultAnchor(data);
	InvalidateRect(data->hWndPreview, NULL, FALSE);
}

static void AnmViewerCmdOnShowFrames(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) param;
	NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) data->editorMgr;
	HWND hWndMdi = nitroPaintStruct->hWndMdi;

	if (data->hWndFrames != NULL) {
		SendMessage(hWndMdi, WM_MDIACTIVATE, (WPARAM) data->hWndFrames, 0);
	} else {
		data->hWndFrames = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, 
			L"NanrFrameClass", L"Frame List", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN,
			CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, hWndMdi, NULL, NULL, NULL);
		SendMessage(data->hWndFrames, NV_INITIALIZE, 0, (LPARAM) data);
	}
}

static void AnmViewerCmdOnSetPlayMode(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) param;

	NANR_SEQUENCE *seq = AnmViewerGetCurrentSequence(data);
	if (seq == NULL) return;

	seq->mode = UiCbGetCurSel(hWndCtl) + NANR_SEQ_MODE_FORWARD;
}

static void AnmViewerCmdOnNewSequence(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) param;

	//create a new sequence.
	data->nanr->nSequences++;
	data->nanr->sequences = (NANR_SEQUENCE *) realloc(data->nanr->sequences, data->nanr->nSequences * sizeof(NANR_SEQUENCE));

	//defaults
	NANR_SEQUENCE *dest = &data->nanr->sequences[data->nanr->nSequences - 1];
	dest->mode = NANR_SEQ_MODE_FORWARD_LOOP;
	dest->type = NANR_SEQ_TYPE_INDEX_SRT | (NANR_SEQ_TYPE_CELL << 16);
	dest->nFrames = 1;
	dest->startFrameIndex = 0;
	dest->frames = (FRAME_DATA *) calloc(1, sizeof(FRAME_DATA));
	dest->frames[0].nFrames = 4;
	dest->frames[0].pad_ = 0xBEEF;
	dest->frames[0].animationData = calloc(1, sizeof(ANIM_DATA_SRT));

	ANIM_DATA_SRT *srt = (ANIM_DATA_SRT *) dest->frames[0].animationData;
	srt->sx = FX32_ONE;
	srt->sy = FX32_ONE;

	ListView_SetItemCount(data->hWndAnimList, data->nanr->nSequences);
	AnmViewerSetCurrentSequence(data, data->nanr->nSequences - 1, TRUE);
}

static void AnmViewerCmdOnToggleForceAffine(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) param;
	data->forceAffine = GetCheckboxChecked(hWndCtl);
	InvalidateRect(data->hWndPreview, NULL, FALSE);
	InvalidateRect(data->hWndAnimList, NULL, FALSE);
}

static void AnmViewerCmdOnToggleForceDoubleSize(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) param;
	data->forceDoubleSize = GetCheckboxChecked(hWndCtl);
	InvalidateRect(data->hWndPreview, NULL, FALSE);
	InvalidateRect(data->hWndAnimList, NULL, FALSE);
}

static void AnmViewerExportSequence(NANRVIEWERDATA *data) {
	NANR_SEQUENCE *seq = AnmViewerGetCurrentSequence(data);
	if (seq == NULL) return;

	LPWSTR path = saveFileDialog(data->hWnd, L"Export GIF", L"GIF Files (*.gif)\0*.gif\0All Files\0*.*\0", L"gif");
	if (path == NULL) return;

	//prepare frames
	int *durations = (int *) calloc(seq->nFrames, sizeof(int));
	COLOR32 **frames = (COLOR32 **) calloc(seq->nFrames, sizeof(COLOR32 **));
	for (int i = 0; i < seq->nFrames; i++) {
		frames[i] = (COLOR32 *) calloc(512 * 256, sizeof(COLOR32));
	}

	//render animation sequence
	for (int i = 0; i < seq->nFrames; i++) {
		durations[i] = (seq->frames[i].nFrames * 1000 + 30) / 60; // frame to millisecond
		AnmViewerRenderFrameFromCurrentSequence(data, frames[i], i, TRUE);

		//reverse color order
		for (int j = 0; j < 512 * 256; j++) {
			COLOR32 c = frames[i][j];
			frames[i][j] = REVERSE(c);
		}
	}

	ImgWriteAnimatedGif(path, frames, 512, 256, durations, seq->nFrames);

	for (int i = 0; i < seq->nFrames; i++) free(frames[i]);
	free(frames);
	free(durations);
	free(path);
}

static LRESULT CALLBACK AnmViewerSeqListSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT idSubclass, DWORD_PTR data_) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) data_;

	switch (msg) {
		case WM_KEYDOWN:
		{
			switch (wParam) {
				case VK_DELETE:
				{
					AnmViewerDeleteCurrentSequence(data);
					break;
				}
			}
			break;
		}
	}

	return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static void AnmViewerOnCreate(NANRVIEWERDATA *data) {
	data->scale = 2;
	data->showBorders = 1;

	data->hWndAnimList = AnmViewerAnimListCreate(data);
	data->hWndNewSequence = CreateButton(data->hWnd, L"New Sequence", 0, 0, 0, 0, FALSE);
	data->hWndPreview = CreateWindow(L"NanrPreviewClass", L"", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL | WS_CLIPSIBLINGS,
		300, 0, 300, 300, data->hWnd, NULL, NULL, NULL);
	FbCreate(&data->fb, data->hWndPreview, 0, 0);

	data->hWndPlayPause = CreateButton(data->hWnd, L"Play", 0, 0, 0, 0, FALSE);
	data->hWndStop = CreateButton(data->hWnd, L"Step", 0, 0, 0, 0, FALSE);
	data->hWndShowFrames = CreateButton(data->hWnd, L"Frames", 0, 0, 0, 0, FALSE);
	data->hWndForceAffine = CreateCheckbox(data->hWnd, L"Force Affine", 0, 0, 0, 0, FALSE);
	data->hWndForceDoubleSize = CreateCheckbox(data->hWnd, L"Force Double Size", 0, 0, 0, 0, FALSE);

	LPCWSTR playModes[] = {
		//L"[\xFF0F\xFFE3\xFFE3\xFFE3] Forward",
		//L"[\xFF0F\xFF0F\xFF0F\xFF0F] Forward Loop",
		//L"[\xFF0F\xFF3C\xFF3F\xFF3F] Backward",
		//L"[\xFF0F\xFF3C\xFF0F\xFF3C] Backward Loop"
		L"[/\xAF\xAF\xAF] Forward",
		L"[////] Forward Loop",
		L"[/\\__] Backward",
		L"[/\\/\\] Backward Loop"
	};
	data->hWndPlayMode = CreateCombobox(data->hWnd, playModes, sizeof(playModes) / sizeof(*playModes), 0, 0, 0, 0, 0);

	UiCtlMgrInit(&data->mgr, data);
	UiCtlMgrAddCommand(&data->mgr, data->hWndPlayPause, BN_CLICKED, AnmViewerCmdOnPlayPause);
	UiCtlMgrAddCommand(&data->mgr, data->hWndStop, BN_CLICKED, AnmViewerCmdOnStop);
	UiCtlMgrAddCommand(&data->mgr, data->hWndShowFrames, BN_CLICKED, AnmViewerCmdOnShowFrames);
	UiCtlMgrAddCommand(&data->mgr, data->hWndPlayMode, CBN_SELCHANGE, AnmViewerCmdOnSetPlayMode);
	UiCtlMgrAddCommand(&data->mgr, data->hWndNewSequence, BN_CLICKED, AnmViewerCmdOnNewSequence);
	UiCtlMgrAddCommand(&data->mgr, data->hWndForceAffine, BN_CLICKED, AnmViewerCmdOnToggleForceAffine);
	UiCtlMgrAddCommand(&data->mgr, data->hWndForceDoubleSize, BN_CLICKED, AnmViewerCmdOnToggleForceDoubleSize);

	SetWindowSubclass(data->hWndAnimList, AnmViewerSeqListSubclassProc, 2, (DWORD_PTR) data);
}

static int AnmViewerOnSize(NANRVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	RECT rcClient;
	GetClientRect(data->hWnd, &rcClient);

	float dpiScale = GetDpiScale();
	int ctlHeight = UI_SCALE_COORD(22, dpiScale);
	int ctlWidth = UI_SCALE_COORD(50, dpiScale);
	int ctlWidthWide = UI_SCALE_COORD(130, dpiScale);
	int cellListWidth = UI_SCALE_COORD(200, dpiScale);

	int ctlY = rcClient.bottom - ctlHeight;
	MoveWindow(data->hWndAnimList, 0, 0, cellListWidth, ctlY, TRUE);
	MoveWindow(data->hWndNewSequence, 0, ctlY, cellListWidth, ctlHeight, TRUE);
	MoveWindow(data->hWndPreview, cellListWidth, ctlHeight, rcClient.right - cellListWidth, rcClient.bottom - ctlHeight, TRUE);

	MoveWindow(data->hWndPlayPause, cellListWidth, 0, ctlWidth, ctlHeight, TRUE);
	MoveWindow(data->hWndStop, cellListWidth + ctlWidth, 0, ctlWidth, ctlHeight, TRUE);

	int cellPropX = cellListWidth + ctlWidth * 2 + UI_SCALE_COORD(10, dpiScale);
	MoveWindow(data->hWndPlayMode, cellPropX, 0, ctlWidthWide, ctlHeight, TRUE);

	int frameButtonX = cellPropX + ctlWidthWide + UI_SCALE_COORD(10, dpiScale);
	MoveWindow(data->hWndShowFrames, frameButtonX, 0, UI_SCALE_COORD(75, dpiScale), ctlHeight, TRUE);

	int forceCtlX = frameButtonX + UI_SCALE_COORD(75, dpiScale) + UI_SCALE_COORD(10, dpiScale);
	MoveWindow(data->hWndForceAffine, forceCtlX, 0, UI_SCALE_COORD(80, dpiScale), ctlHeight, TRUE);
	MoveWindow(data->hWndForceDoubleSize, forceCtlX + UI_SCALE_COORD(80, dpiScale), 0, UI_SCALE_COORD(125, dpiScale), ctlHeight, TRUE);

	if (wParam == SIZE_RESTORED) InvalidateRect(data->hWndPreview, NULL, TRUE); //full update
	return DefMDIChildProc(data->hWnd, WM_SIZE, wParam, lParam);
}

static void AnmViewerPreviewCenter(NANRVIEWERDATA *data) {
	//get client
	RECT rcClient;
	GetClientRect(data->hWndPreview, &rcClient);

	//get view size
	int viewWidth = 512 * data->scale;
	int viewHeight = 256 * data->scale;

	//check dimensions of view
	if (rcClient.right < viewWidth) {
		//set scroll H
		SCROLLINFO scroll = { 0 };
		scroll.cbSize = sizeof(scroll);
		scroll.fMask = SIF_POS;
		scroll.nPos = (viewWidth - rcClient.right) / 2;
		SetScrollInfo(data->hWndPreview, SB_HORZ, &scroll, TRUE);
	}
	if (rcClient.bottom < viewHeight) {
		//set scroll V
		SCROLLINFO scroll = { 0 };
		scroll.cbSize = sizeof(scroll);
		scroll.fMask = SIF_POS;
		scroll.nPos = (viewHeight - rcClient.bottom) / 2;
		SetScrollInfo(data->hWndPreview, SB_VERT, &scroll, TRUE);
	}
}

static void AnmViewerOnInitialize(NANRVIEWERDATA *data, NANR *nanr, LPCWSTR path) {
	if (path != NULL) {
		EditorSetFile(data->hWnd, path);
	}

	//own data
	data->nanr = nanr;

	data->frameData.contentWidth = 512 * data->scale;
	data->frameData.contentHeight = 256 * data->scale;
	data->currentAnim = -1;
	AnmViewerPreviewCenter(data);

	//initialize listview
	SendMessage(data->hWndAnimList, WM_SETREDRAW, 0, 0);

	//set item count
	ListView_SetItemCount(data->hWndAnimList, data->nanr->nSequences);

	//init imagelist count
	HIMAGELIST himl = ListView_GetImageList(data->hWndAnimList, LVSIL_NORMAL);
	ImageList_SetImageCount(himl, 1);

	AnmViewerSetCurrentSequence(data, 0, TRUE);
	AnmViewerSetDefaultAnchor(data);
	SendMessage(data->hWndAnimList, WM_SETREDRAW, 1, 0);
}

static void AnmViewerOnDestroy(NANRVIEWERDATA *data) {
	FbDestroy(&data->fb);
	UiCtlMgrFree(&data->mgr);

	if (data->hWndFrames != NULL) {
		DestroyChild(data->hWndFrames);
	}
}

static void AnmViewerOnMenuCommand(NANRVIEWERDATA *data, int idMenu) {
	switch (idMenu) {
		case ID_VIEW_GRIDLINES:
		case ID_VIEW_RENDERTRANSPARENCY:
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
		case ID_FILE_SAVE:
			EditorSave(data->hWnd);
			break;
		case ID_FILE_SAVEAS:
			EditorSaveAs(data->hWnd);
			break;
		case ID_FILE_EXPORT:
			AnmViewerExportSequence(data);
			break;
	}
}

static void AnmViewerOnCommand(NANRVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	UiCtlMgrOnCommand(&data->mgr, data->hWnd, wParam, lParam);

	if (lParam == 0 && HIWORD(wParam) == 0) {
		AnmViewerOnMenuCommand(data, LOWORD(wParam));
	}
}

static void AnmViewerOnTimer(NANRVIEWERDATA *data, int idTimer) {
	switch (idTimer) {
		case NANRVIEWER_TIMER_TICK:
			AnmViewerTickPlayback(data);
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
	}
}

static LRESULT AnmViewerOnNotify(NANRVIEWERDATA *data, WPARAM wParam, LPNMHDR hdr) {
	if (hdr->hwndFrom == data->hWndAnimList) {
		switch (hdr->code) {
			case NM_CLICK:
			case NM_DBLCLK:
			case NM_RCLICK:
			case NM_RDBLCLK:
			{
				LPNMITEMACTIVATE nma = (LPNMITEMACTIVATE) hdr;
				if (nma->iItem == -1) {
					//item being unselected. Mark variable to cancel the deselection.
					ListView_SetItemState(data->hWndAnimList, data->currentAnim, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				}
				break;
			}
			case LVN_ITEMCHANGED:
			{
				LPNMLISTVIEW nm = (LPNMLISTVIEW) hdr;
				if (nm->uNewState & LVIS_SELECTED) {
					//selection changed
					AnmViewerSetCurrentSequence(data, nm->iItem, FALSE);
				}
				break;
			}
			case LVN_GETDISPINFO:
			{
				NMLVDISPINFO *di = (NMLVDISPINFO *) hdr;

				//fill out item structure
				if (di->item.mask & LVIF_COLFMT) {
					static int colFmt[2] = { 0 };
					di->item.piColFmt = colFmt;
				}
				if (di->item.mask & LVIF_COLUMNS) {
					di->item.cColumns = 1;
					di->item.puColumns[0] = 1;
				}
				if (di->item.mask & LVIF_IMAGE) {
					di->item.iImage = 0; // re-use imagelist images
					AnmViewerRenderGlyphListImage(data, di->item.iItem);
				}
				if (di->item.mask & LVIF_TEXT) {
					//buffer is valid until the next call
					if (di->item.iSubItem == 0) {
						wsprintfW(data->listviewItemBuffers[0], L"[%d] Sequence %d", di->item.iItem, di->item.iItem);
						di->item.pszText = data->listviewItemBuffers[0];
					} else {
						wsprintfW(data->listviewItemBuffers[1], L"%d frames", data->nanr->sequences[di->item.iItem]);
						di->item.pszText = data->listviewItemBuffers[1];
					}
				}

				return TRUE;
			}
		}
	}
	return DefWindowProc(data->hWnd, WM_NOTIFY, wParam, (LPARAM) hdr);
}

static LRESULT CALLBACK AnmViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) EditorGetData(hWnd);

	switch (msg) {
		case WM_CREATE:
			AnmViewerOnCreate(data);
			break;
		case NV_INITIALIZE:
			AnmViewerOnInitialize(data, (NANR *) lParam, (LPCWSTR) wParam);
			break;
		case NV_ZOOMUPDATED:
			SendMessage(data->hWndPreview, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			break;
		case WM_PAINT:
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
		case WM_TIMER:
			AnmViewerOnTimer(data, wParam);
			break;
		case WM_COMMAND:
			AnmViewerOnCommand(data, wParam, lParam);
			break;
		case WM_NOTIFY:
			return AnmViewerOnNotify(data, wParam, (LPNMHDR) lParam);
		case WM_KEYDOWN:
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
		case WM_SIZE:
			return AnmViewerOnSize(data, wParam, lParam);
		case WM_DESTROY:
			AnmViewerOnDestroy(data);
			break;
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}


static void AvgPoint(int *ptOut, const int *pt1, const int *pt2) {
	int ptX = pt1[0] + pt2[0];
	int ptY = pt1[1] + pt2[1];

	//round
	if (ptX >= 0) ptX++;
	else ptX--;
	if (ptY >= 0) ptY++;
	else ptY--;

	ptOut[0] = ptX / 2;
	ptOut[1] = ptY / 2;
}

static void AnmViewerRenderFrameFromCurrentSequence(NANRVIEWERDATA *data, COLOR32 *dest, int iFrm, BOOL fillBG) {
	//get data
	NCLR *nclr = (NCLR *) AnmViewerGetAssociatedObject(data, FILE_TYPE_PALETTE);
	NCGR *ncgr = (NCGR *) AnmViewerGetAssociatedObject(data, FILE_TYPE_CHARACTER);
	NCER *ncer = (NCER *) AnmViewerGetAssociatedObject(data, FILE_TYPE_CELL);

	//fill BG
	if (fillBG) {
		if (nclr != NULL) {
			COLOR32 bg = ColorConvertFromDS(nclr->colors[0]);
			for (int i = 0; i < 256 * 512; i++) {
				dest[i] = bg;
			}
		} else {
			memset(dest, 0, 256 * 512 * sizeof(COLOR32));
		}
	}

	if (ncer == NULL) return;

	//get frame data
	ANIM_DATA_SRT frm;
	if (!AnmViewerGetAnimFrame(data, data->currentAnim, iFrm, &frm, NULL)) return;
	if (frm.index < 0 || frm.index >= ncer->nCells) return;

	//get referenced cell
	NCER_CELL *cell = &ncer->cells[frm.index];
	double sx = frm.sx / 4096.0;
	double sy = frm.sy / 4096.0;
	double rot = (frm.rotZ / 65536.0) * RAD_360DEG;

	double mtx[2][2] = { { 1.0, 0.0 }, { 0.0, 1.0 } }, trans[2] = { 0 };
	AnmCalcTransformMatrix(0.0, 0.0, sx, sy, rot, (double) frm.px, (double) frm.py, &mtx[0][0], trans);
	CellRender(dest, NULL, ncer, ncgr, nclr, frm.index, cell,
		FloatToInt(trans[0]), FloatToInt(trans[1]),
		mtx[0][0], mtx[0][1], mtx[1][0], mtx[1][1],
		data->forceAffine, data->forceDoubleSize);
}

static void AnmViewerPreviewOnPaint(NANRVIEWERDATA *data) {
	HWND hWnd = data->hWndPreview;
	PAINTSTRUCT ps;
	HDC hDC = BeginPaint(hWnd, &ps);

	//get data
	NCLR *nclr = (NCLR *) AnmViewerGetAssociatedObject(data, FILE_TYPE_PALETTE);
	NCER *ncer = (NCER *) AnmViewerGetAssociatedObject(data, FILE_TYPE_CELL);

	//get frame data
	ANIM_DATA_SRT frm;
	int frmExist = AnmViewerGetCurrentAnimFrame(data, &frm, NULL);

	//get referenced cell
	NCER_CELL *cell = NULL;
	if (frmExist && ncer != NULL && frm.index < ncer->nCells) {
		cell = &ncer->cells[frm.index];
	}

	double mtx[2][2] = { { 1.0f, 0.0f }, { 0.0f, 1.0f } }, trans[2] = { 0 };
	AnmViewerGetCurrentFrameTransform(data, &mtx[0][0], trans);

	//render
	memset(data->cellRender, 0, sizeof(data->cellRender));
	AnmViewerRenderFrameFromCurrentSequence(data, data->cellRender, data->currentFrame, FALSE);

	//ensure framebuffer size
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);
	FbSetSize(&data->fb, rcClient.right, rcClient.bottom);

	int scrollX, scrollY;
	AnmViewerPreviewGetScroll(data, &scrollX, &scrollY);

	int viewWidth = 512 * data->scale - scrollX;
	int viewHeight = 256 * data->scale - scrollY;
	if (viewWidth > rcClient.right) viewWidth = rcClient.right;
	if (viewHeight > rcClient.bottom) viewHeight = rcClient.bottom;

	COLOR32 bgColor = 0;
	if (nclr != NULL && nclr->nColors >= 1) {
		bgColor = ColorConvertFromDS(nclr->colors[0]);
		bgColor = REVERSE(bgColor);
	}

	for (int y = 0; y < rcClient.bottom; y++) {
		for (int x = 0; x < rcClient.right; x++) {
			int srcX = (x + scrollX) / data->scale, srcY = (y + scrollY) / data->scale;
			
			//sample coordinate
			COLOR32 sample = 0xFFF0F0F0;
			if (srcX < 512 && srcY < 256) {
				sample = data->cellRender[srcY * 512 + srcX];
			}

			if ((sample >> 24) == 0) {
				if (g_configuration.renderTransparent) {
					//render transparent checkerboard
					COLOR32 checker[] = { 0xFFFFFF, 0xC0C0C0 };
					sample = checker[((x ^ y) >> 2) & 1];
				} else {
					//render backdrop color
					sample = bgColor;
				}
			}

			data->fb.px[x + y * data->fb.width] = sample;
		}
	}

	//render borders
	if (data->showBorders) {
		CellViewerRenderGridlines(&data->fb, data->scale, scrollX, scrollY);
	}

	//render bounds
	if (cell != NULL && !data->playing) {
		int boxX, boxY, boxW, boxH;
		AnmViewerGetCellBounds(cell, &boxX, &boxY, &boxW, &boxH);

		int cx = FloatToInt(trans[0]);
		int cy = FloatToInt(trans[1]);

		COLOR32 lineCols[] = { 0x00FFFF, 0x00FFFF, 0x00FFFF, 0x00FFFF }; // up down left right
		int hit = AnmViewerGetEffectiveHit(data);
		if ((hit & ANM_HIT_TYPE_MASK) == ANM_HIT_BOX) {

			//mark edges we hit.
			int flg = hit & ANM_HIT_FLAG_MASK;
			if (flg == 0 || (flg & ANM_HIT_U)) lineCols[0] = 0xFFFF00;
			if (flg == 0 || (flg & ANM_HIT_D)) lineCols[1] = 0xFFFF00;
			if (flg == 0 || (flg & ANM_HIT_L)) lineCols[2] = 0xFFFF00;
			if (flg == 0 || (flg & ANM_HIT_R)) lineCols[3] = 0xFFFF00;
		}

		AnmViewerDrawBoxRot(&data->fb, lineCols, 
			boxX * data->scale,
			boxY * data->scale,
			boxW * data->scale,
			boxH * data->scale,
			256 * data->scale - scrollX + cx * data->scale,
			128 * data->scale - scrollY + cy * data->scale,
			mtx[0][0], mtx[0][1], mtx[1][0], mtx[1][1]);
	}

	//draw rotation guide circle
	if (!data->playing) {
		int hit = AnmViewerGetEffectiveHit(data);
		COLOR32 col = (hit == ANM_HIT_ROT_CIRCLE) ? 0x00FFFF : 0x00FF00; // yellow when hit

		int cx, cy, r;
		AnmViewerGetRotCircle(data, &cx, &cy, &r);
		FbRenderSolidCircle(&data->fb, cx + 256 * data->scale - scrollX, cy + 128 * data->scale - scrollY, r, col);
	}

	//draw anchor
	if (!data->playing) {
		//draw anchor
		int anchorX = data->anchorX * data->scale + 256 * data->scale - scrollX;
		int anchorY = data->anchorY * data->scale + 128 * data->scale - scrollY;

		int hit = AnmViewerGetEffectiveHit(data);

		COLOR32 col = (hit == ANM_HIT_ANCHOR ? 0xFF0000 : 0xFFFF00);
		FbDrawLine(&data->fb, col, anchorX - ANCHOR_SIZE / 2, anchorY - ANCHOR_SIZE / 2, anchorX + ANCHOR_SIZE / 2, anchorY - ANCHOR_SIZE / 2);
		FbDrawLine(&data->fb, col, anchorX + ANCHOR_SIZE / 2, anchorY - ANCHOR_SIZE / 2, anchorX + ANCHOR_SIZE / 2, anchorY + ANCHOR_SIZE / 2);
		FbDrawLine(&data->fb, col, anchorX + ANCHOR_SIZE / 2, anchorY + ANCHOR_SIZE / 2, anchorX - ANCHOR_SIZE / 2, anchorY + ANCHOR_SIZE / 2);
		FbDrawLine(&data->fb, col, anchorX - ANCHOR_SIZE / 2, anchorY + ANCHOR_SIZE / 2, anchorX - ANCHOR_SIZE / 2, anchorY - ANCHOR_SIZE / 2);
	}

	//draw rotation point
	if (!data->playing) {
		//draw rotation point
		int anchorX = data->rotPtX * data->scale + 256 * data->scale - scrollX;
		int anchorY = data->rotPtY * data->scale + 128 * data->scale - scrollY;

		int hit = AnmViewerGetEffectiveHit(data);

		COLOR32 col = (hit == ANM_HIT_ROT_POINT ? 0xFF0000 : 0xFFFF00);
		FbDrawLine(&data->fb, col, anchorX - ANCHOR_SIZE / 2, anchorY - ANCHOR_SIZE / 2, anchorX + ANCHOR_SIZE / 2, anchorY - ANCHOR_SIZE / 2);
		FbDrawLine(&data->fb, col, anchorX + ANCHOR_SIZE / 2, anchorY - ANCHOR_SIZE / 2, anchorX + ANCHOR_SIZE / 2, anchorY + ANCHOR_SIZE / 2);
		FbDrawLine(&data->fb, col, anchorX + ANCHOR_SIZE / 2, anchorY + ANCHOR_SIZE / 2, anchorX - ANCHOR_SIZE / 2, anchorY + ANCHOR_SIZE / 2);
		FbDrawLine(&data->fb, col, anchorX - ANCHOR_SIZE / 2, anchorY + ANCHOR_SIZE / 2, anchorX - ANCHOR_SIZE / 2, anchorY - ANCHOR_SIZE / 2);
	}

	FbDraw(&data->fb, hDC, 0, 0, rcClient.right, rcClient.bottom, 0, 0);
	EndPaint(hWnd, &ps);
}

static void AnmViewerPreviewOnLButtonDown(NANRVIEWERDATA *data) {
	POINT ptCursor;
	GetCursorPos(&ptCursor);
	ScreenToClient(data->hWndPreview, &ptCursor);

	int scrollX, scrollY;
	AnmViewerPreviewGetScroll(data, &scrollX, &scrollY);

	int hit = AnmViewerHitTest(data, ptCursor.x, ptCursor.y);
	data->hit = hit;

	data->mouseDown = 1;
	data->mouseDownHit = data->hit;
	SetCapture(data->hWndPreview);
	SetFocus(data->hWndPreview);
	InvalidateRect(data->hWndPreview, NULL, FALSE);

	//put coordinate
	data->mouseDownX = ptCursor.x;
	data->mouseDownY = ptCursor.y;
	data->mouseDownAnchorX = data->anchorX;
	data->mouseDownAnchorY = data->anchorY;
	data->rotAngleOffset = 0.0f;

	//respond to hit
	int hitType = hit & ANM_HIT_TYPE_MASK;
	int hitFlg = hit & ANM_HIT_FLAG_MASK;

	switch (hitType) {
		case ANM_HIT_NOWHERE:
			break; // do nothing
		case ANM_HIT_BOX:
		case ANM_HIT_ROT_POINT:
		case ANM_HIT_ROT_CIRCLE:
		{
			//set initial transform
			ANIM_DATA_SRT frm;
			if (!AnmViewerGetCurrentAnimFrame(data, &frm, NULL)) break;

			AnmViewerDecodeTransform(&data->transStart, &frm);

			if (hitType == ANM_HIT_ROT_CIRCLE) {
				//start rotation on circle, calculate rotation offset
				double rotX = (double) (ptCursor.x + scrollX - 256 * data->scale - data->anchorX * data->scale);
				double rotY = (double) (ptCursor.y + scrollY - 128 * data->scale - data->anchorY * data->scale);
				double mouseRot = AnmViewerGetPtAngle(data, rotX, rotY);

				data->rotAngleOffset = data->transStart.rot - mouseRot;
			}
			break;
		}
	}
}

static void AnmViewerPreviewOnLButtonUp(NANRVIEWERDATA *data) {
	data->mouseDown = 0;
	data->mouseDownHit = ANM_HIT_NOWHERE;
	ReleaseCapture();
	InvalidateRect(data->hWndPreview, NULL, FALSE);
}

static void AnmViewerPreviewOnMouseMove(NANRVIEWERDATA *data) {
	POINT ptCursor;
	GetCursorPos(&ptCursor);
	ScreenToClient(data->hWndPreview, &ptCursor);

	int hit = AnmViewerHitTest(data, ptCursor.x, ptCursor.y);
	if (hit != data->hit) InvalidateRect(data->hWndPreview, NULL, FALSE);
	data->hit = hit;

	int scrollX, scrollY;
	AnmViewerPreviewGetScroll(data, &scrollX, &scrollY);

	int shiftState = GetKeyState(VK_SHIFT) < 0;

	//if mouse down
	if (data->mouseDown) {
		switch (data->mouseDownHit & ANM_HIT_TYPE_MASK) {
			case ANM_HIT_NOWHERE:
				break; // no processing
			case ANM_HIT_BOX:
			{
				int flg = data->mouseDownHit & ANM_HIT_FLAG_MASK;

				AnmTransSrt srt;
				memcpy(&srt, &data->transStart, sizeof(AnmTransSrt));

				if (flg == 0) {
					//translaion
					int dx = (ptCursor.x - data->mouseDownX) / data->scale;
					int dy = (ptCursor.y - data->mouseDownY) / data->scale;

					srt.tx += (double) dx;
					srt.ty += (double) dy;

					//move anchor
					AnmViewerSetAnchor(data, data->mouseDownAnchorX + dx, data->mouseDownAnchorY + dy);
				} else {
					//scale
					double centeredX1 = (double) (data->mouseDownX + scrollX - 256 * data->scale - data->anchorX * data->scale);
					double centeredY1 = (double) (data->mouseDownY + scrollY - 128 * data->scale - data->anchorY * data->scale);
					double centeredX2 = (double) (ptCursor.x + scrollX - 256 * data->scale - data->anchorX * data->scale);
					double centeredY2 = (double) (ptCursor.y + scrollY - 128 * data->scale - data->anchorY * data->scale);

					//compute basis vectors for the current rotation
					double cosR = cos(data->transStart.rot);
					double sinR = sin(data->transStart.rot);
					double vecUX = -sinR;
					double vecUY = cosR;
					double vecRX = cosR;
					double vecRY = sinR;

					double gx = 1.0f, gy = 1.0f; // growth factors
					if (flg & (ANM_HIT_R | ANM_HIT_L)) {
						gx = (centeredX2 * vecRX + centeredY2 * vecRY) / (centeredX1 * vecRX + centeredY1 * vecRY);
					}
					if (flg & (ANM_HIT_U | ANM_HIT_D)) {
						gy = (centeredX2 * vecUX + centeredY2 * vecUY) / (centeredX1 * vecUX + centeredY1 * vecUY);
					}

					double sx2 = srt.sx * gx;
					double sy2 = srt.sy * gy;

					//shift pressed, round to integer scales
					if (shiftState) {
						//X scale
						if (sx2 >= 1.0f || sx2 <= -1.0f) {
							//round scale to integer
							sx2 = (double) FloatToInt(sx2);
						} else if (sx2 != 0.0f) {
							//round inverse scale to integer
							sx2 = 1.0f / sx2;
							sx2 = (double) FloatToInt(sx2);
							sx2 = 1.0f / sx2;
						}

						//Y scale
						if (sy2 >= 1.0f || sy2 <= -1.0f) {
							//round scale to integer
							sy2 = (double) FloatToInt(sy2);
						} else if (sy2 != 0.0f) {
							//round inverse scale to integer
							sy2 = 1.0f / sy2;
							sy2 = (double) FloatToInt(sy2);
							sy2 = 1.0f / sy2;
						}
					}

					AnmViewerApplyScaleTransform((double) data->anchorX, (double) data->anchorY, &srt.tx, &srt.ty, &srt.sx, &srt.sy, srt.rot, sx2, sy2);
				}

				//put
				ANIM_DATA_SRT frm;
				AnmViewerGetCurrentAnimFrame(data, &frm, NULL);
				AnmViewerEncodeTransform(&frm, &srt);
				AnmViewerPutCurrentAnimFrame(data, &frm, NULL);
				AnmViewerSetRotationPoint(data);
				InvalidateRect(data->hWndPreview, NULL, FALSE);

				break;
			}
			case ANM_HIT_ANCHOR:
			{
				//move anchor
				int dx = (ptCursor.x - data->mouseDownX) / data->scale;
				int dy = (ptCursor.y - data->mouseDownY) / data->scale;
				int newX = data->mouseDownAnchorX + dx;
				int newY = data->mouseDownAnchorY + dy;

				//if shift key held, clamp to one of a few points
				if (shiftState) {
					int clampCoords[10][2] = {
						{ 0, 0 } // 0: origin
					};

					ANIM_DATA_SRT frm;
					NCER *ncer = (NCER *) AnmViewerGetAssociatedObject(data, FILE_TYPE_CELL);
					NCER_CELL *cell = NULL;

					if (AnmViewerGetCurrentAnimFrame(data, &frm, NULL) && ncer != NULL && frm.index >= 0 && frm.index < ncer->nCells) {
						NCER_CELL *cell = &ncer->cells[frm.index];

						double mtx[2][2], trans[2];
						AnmViewerGetCurrentFrameTransform(data, &mtx[0][0], &trans[0]);

						int cellX, cellY, cellW, cellH;
						AnmViewerGetCellBounds(cell, &cellX, &cellY, &cellW, &cellH);

						AnmViewerGetCellBoundCorners(
							&clampCoords[1][0], // 1: top-left
							&clampCoords[3][0], // 3: top-right
							&clampCoords[7][0], // 7: bottom-left
							&clampCoords[9][0], // 9: bottom-right
							cellX, cellY, cellW, cellH,
							mtx[0][0], mtx[0][1], mtx[1][0], mtx[1][1],
							FloatToInt(trans[0]),
							FloatToInt(trans[1]));

						//interpolate edge points
						AvgPoint(&clampCoords[2][0], &clampCoords[1][0], &clampCoords[3][0]);
						AvgPoint(&clampCoords[8][0], &clampCoords[7][0], &clampCoords[9][0]);
						AvgPoint(&clampCoords[4][0], &clampCoords[1][0], &clampCoords[7][0]);
						AvgPoint(&clampCoords[6][0], &clampCoords[3][0], &clampCoords[9][0]);
						AvgPoint(&clampCoords[5][0], &clampCoords[2][0], &clampCoords[8][0]);

						//get nearest
						int bestD2 = INT_MAX;
						int bestI = 0;
						for (int i = 0; i < sizeof(clampCoords) / sizeof(clampCoords[0]); i++) {
							int cdx = clampCoords[i][0] - newX;
							int cdy = clampCoords[i][1] - newY;
							int d2 = cdx * cdx + cdy * cdy;
							if (d2 < bestD2) {
								bestD2 = d2;
								bestI = i;
							}
						}

						//best coordinates
						newX = clampCoords[bestI][0];
						newY = clampCoords[bestI][1];
					}
				}

				AnmViewerSetAnchor(data, newX, newY);
				AnmViewerSetRotationPoint(data);
				InvalidateRect(data->hWndPreview, NULL, FALSE);
				break;
			}
			case ANM_HIT_ROT_POINT:
			case ANM_HIT_ROT_CIRCLE:
			{
				AnmTransSrt srt;
				memcpy(&srt, &data->transStart, sizeof(AnmTransSrt));

				//rotation point
				double rotX = (double) (ptCursor.x + scrollX - 256 * data->scale - data->anchorX * data->scale);
				double rotY = (double) (ptCursor.y + scrollY - 128 * data->scale - data->anchorY * data->scale);
				double newrot = AnmViewerGetPtAngle(data, rotX, rotY);
				newrot += data->rotAngleOffset;

				//if shift pressed, round to 1/8 rotations
				if (shiftState) {
					newrot /= RAD_360DEG;
					newrot *= 8.0f;
					newrot = (double) FloatToInt(newrot);
					newrot /= 8.0f;
					newrot *= RAD_360DEG;
				}

				AnmViewerApplyRotateTransform((double) data->anchorX, (double) data->anchorY,
					&srt.tx, &srt.ty, srt.sx, srt.sy, &srt.rot, newrot);

				ANIM_DATA_SRT frm;
				AnmViewerGetCurrentAnimFrame(data, &frm, NULL);
				AnmViewerEncodeTransform(&frm, &srt);
				AnmViewerPutCurrentAnimFrame(data, &frm, NULL);
				AnmViewerSetRotationPoint(data);
				InvalidateRect(data->hWndPreview, NULL, FALSE);
				break;
			}
		}
	}

	//track mouse event
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.hwndTrack = data->hWndPreview;
	tme.dwFlags = TME_LEAVE;
	TrackMouseEvent(&tme);
}

static LRESULT AnmViewerPreviewSetCursor(NANRVIEWERDATA *data, WPARAM wParam, LPARAM lParam) {
	HWND hWndHit = (HWND) wParam;
	if (hWndHit != data->hWndPreview) return DefWindowProc(data->hWndPreview, WM_SETCURSOR, wParam, lParam);
	if (LOWORD(lParam) != HTCLIENT) return DefWindowProc(data->hWndPreview, WM_SETCURSOR, wParam, lParam);

	AnmViewerPreviewOnMouseMove(data);

	int hit = AnmViewerGetEffectiveHit(data);
	HCURSOR hCursor = AnmViewerGetHitCursor(data, hit);
	SetCursor(hCursor);
	return TRUE;
}

static void AnmViewerPreviewOnRecalculate(NANRVIEWERDATA *data) {
	int contentWidth = 512 * data->scale, contentHeight = 256 * data->scale;

	SCROLLINFO info;
	info.cbSize = sizeof(info);
	info.nMin = 0;
	info.nMax = contentWidth;
	info.fMask = SIF_RANGE;
	SetScrollInfo(data->hWndPreview, SB_HORZ, &info, TRUE);

	info.nMax = contentHeight;
	SetScrollInfo(data->hWndPreview, SB_VERT, &info, TRUE);
	RECT rcClient;
	GetClientRect(data->hWndPreview, &rcClient);
	SendMessage(data->hWndPreview, WM_SIZE, 0, rcClient.right | (rcClient.bottom << 16));
}

static void AnmViewerOnKeyDown(NANRVIEWERDATA *data, int vk) {
	switch (vk) {
		case VK_UP:
		case VK_DOWN:
		case VK_LEFT:
		case VK_RIGHT:
		{
			ANIM_DATA_SRT frm;
			if (!AnmViewerGetCurrentAnimFrame(data, &frm, NULL)) break;

			int anchorX = data->anchorX, anchorY = data->anchorY;

			//adjust translation
			if (vk == VK_UP) frm.py--, anchorY--;
			if (vk == VK_DOWN) frm.py++, anchorY++;
			if (vk == VK_LEFT) frm.px--, anchorX--;
			if (vk == VK_RIGHT) frm.px++, anchorX++;

			AnmViewerSetAnchor(data, anchorX, anchorY);
			AnmViewerPutCurrentAnimFrame(data, &frm, NULL);
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
		}
	}
}

static LRESULT CALLBACK AnmViewerPreviewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndEditor = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
	NANRVIEWERDATA *data = EditorGetData(hWndEditor);

	if (data != NULL) {
		data->frameData.contentWidth = 512 * data->scale;
		data->frameData.contentHeight = 256 * data->scale;

		if (GetWindowLongPtr(hWnd, 0) == 0) {
			SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
		}
	}

	switch (msg) {
		case WM_PAINT:
			AnmViewerPreviewOnPaint(data);
			break;
		case WM_LBUTTONDOWN:
			AnmViewerPreviewOnLButtonDown(data);
			break;
		case WM_LBUTTONUP:
			AnmViewerPreviewOnLButtonUp(data);
			break;
		case WM_MOUSEMOVE:
			AnmViewerPreviewOnMouseMove(data);
			break;
		case WM_SETCURSOR:
			return AnmViewerPreviewSetCursor(data, wParam, lParam);
		case WM_KEYDOWN:
			AnmViewerOnKeyDown(data, wParam);
			break;
		case NV_RECALCULATE:
			AnmViewerPreviewOnRecalculate(data);
			break;
		case WM_SIZE:
		{
			UpdateScrollbarVisibility(hWnd);

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = data->frameData.contentWidth;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = data->frameData.contentHeight;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			return DefChildProc(hWnd, msg, wParam, lParam);
		}
		case WM_HSCROLL:
		case WM_VSCROLL:
		case WM_MOUSEWHEEL:
			return DefChildProc(hWnd, msg, wParam, lParam);
		case WM_ERASEBKGND:
			return 1;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK AnmViewerFrameListSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data_) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) data_;

	switch (msg) {
		case WM_ERASEBKGND:
			//prevent erasing (we will paint the whole client area anyway)
			return 1;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			//create offscreen DC
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			HDC hCompatDC = CreateCompatibleDC(hDC);
			HBITMAP hbm = CreateCompatibleBitmap(hDC, rcClient.right, rcClient.bottom);
			SelectObject(hCompatDC, hbm);

			//fill background (off-screen buffer to prevent flicker)
			HBRUSH hbrBack = GetSysColorBrush(COLOR_WINDOW);
			HPEN hNullPen = GetStockObject(NULL_PEN);
			SelectObject(hCompatDC, hbrBack);
			SelectObject(hCompatDC, hNullPen);
			Rectangle(hCompatDC, 0, 0, rcClient.right + 1, rcClient.bottom + 1);

			//forward to default proc
			DefSubclassProc(hWnd, msg, (WPARAM) hCompatDC, 0);

			BitBlt(hDC, 0, 0, rcClient.right, rcClient.bottom, hCompatDC, 0, 0, SRCCOPY);
			DeleteDC(hCompatDC);
			DeleteObject(hbm);

			EndPaint(hWnd, &ps);
			return 0;
		}
		case WM_CONTEXTMENU:
		{
			HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 8);

			//own the menu with the main cell editor window
			POINT mouse;
			GetCursorPos(&mouse);
			TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, data->hWndFrames, NULL);
			DeleteObject(hPopup);
			break;
		}
		case WM_KEYDOWN:
		{
			//for keydown for Del delete frame
			if (wParam == VK_DELETE) {
				AnmViewerDeleteCurrentFrame(data);
			}
			break;
		}
	}

	return DefSubclassProc(hWnd, msg, wParam, lParam);
}


// ----- frame list procedures

static void AnmViewerPutCurrentAnimFrameResetAnchor(NANRVIEWERDATA *data, const ANIM_DATA_SRT *srt, const int *pDuration) {
	AnmViewerPutCurrentAnimFrame(data, srt, pDuration);
	AnmViewerSetDefaultAnchor(data);
	InvalidateRect(data->hWndAnimList, NULL, FALSE);
}

static void AnmViewerCmdSetIndex(NANRVIEWERDATA *data) {
	WCHAR buf[16] = { 0 };
	ANIM_DATA_SRT srt;
	AnmViewerGetCurrentAnimFrame(data, &srt, NULL);
	wsprintfW(buf, L"%d", srt.index);

	PromptUserText(data->hWnd, L"Cell Index", L"Cell Index:", buf, sizeof(buf) / sizeof(buf[0]));
	srt.index = (unsigned short) _wtol(buf);
	AnmViewerPutCurrentAnimFrameResetAnchor(data, &srt, NULL);
}

static void AnmViewerCmdSetDuration(NANRVIEWERDATA *data) {
	WCHAR buf[16] = { 0 };
	int duration;
	AnmViewerGetCurrentAnimFrame(data, NULL, &duration);
	wsprintfW(buf, L"%d", duration);

	PromptUserText(data->hWnd, L"Enter Duration", L"Duration:", buf, sizeof(buf) / sizeof(buf[0]));
	duration = _wtol(buf);
	AnmViewerPutCurrentAnimFrameResetAnchor(data, NULL, &duration);
}

static void AnmViewerCmdSetX(NANRVIEWERDATA *data) {
	WCHAR buf[16] = { 0 };
	ANIM_DATA_SRT srt;
	AnmViewerGetCurrentAnimFrame(data, &srt, NULL);
	wsprintfW(buf, L"%d", srt.px);

	PromptUserText(data->hWnd, L"Enter X Translation", L"X:", buf, sizeof(buf) / sizeof(buf[0]));
	srt.px = (short) _wtol(buf);
	AnmViewerPutCurrentAnimFrameResetAnchor(data, &srt, NULL);
}

static void AnmViewerCmdSetY(NANRVIEWERDATA *data) {
	WCHAR buf[16] = { 0 };
	ANIM_DATA_SRT srt;
	AnmViewerGetCurrentAnimFrame(data, &srt, NULL);
	wsprintfW(buf, L"%d", srt.py);

	PromptUserText(data->hWnd, L"Enter Y Translation", L"Y:", buf, sizeof(buf) / sizeof(buf[0]));
	srt.py = (short) _wtol(buf);
	AnmViewerPutCurrentAnimFrameResetAnchor(data, &srt, NULL);
}

static void AnmViewerCmdSetScaleX(NANRVIEWERDATA *data) {
	WCHAR buf[16] = { 0 };
	ANIM_DATA_SRT srt;
	AnmViewerGetCurrentAnimFrame(data, &srt, NULL);
	FormatFxToString(buf, srt.sx);

	PromptUserText(data->hWnd, L"Enter X Scale", L"X:", buf, sizeof(buf) / sizeof(buf[0]));
	srt.sx = FloatToInt(my_wtof(buf) * 4096.0f);
	AnmViewerPutCurrentAnimFrameResetAnchor(data, &srt, NULL);
}

static void AnmViewerCmdSetScaleY(NANRVIEWERDATA *data) {
	WCHAR buf[16] = { 0 };
	ANIM_DATA_SRT srt;
	AnmViewerGetCurrentAnimFrame(data, &srt, NULL);
	FormatFxToString(buf, srt.sy);

	PromptUserText(data->hWnd, L"Enter Y Scale", L"Y:", buf, sizeof(buf) / sizeof(buf[0]));
	srt.sy = FloatToInt(my_wtof(buf) * 4096.0f);
	AnmViewerPutCurrentAnimFrameResetAnchor(data, &srt, NULL);
}

static void AnmViewerCmdSetRotation(NANRVIEWERDATA *data) {
	WCHAR buf[16] = { 0 };
	ANIM_DATA_SRT srt;
	AnmViewerGetCurrentAnimFrame(data, &srt, NULL);
	FormatAngleToString(buf, srt.rotZ);

	PromptUserText(data->hWnd, L"Enter Rotation", L"Rotation:", buf, sizeof(buf) / sizeof(buf[0]));
	srt.rotZ = FloatToInt(my_wtof(buf) * 65536.0f / 360.0f) & 0xFFFF;
	AnmViewerPutCurrentAnimFrameResetAnchor(data, &srt, NULL);
}

static void AnmViewerInsertFrame(NANRVIEWERDATA *data, int i) {
	NANR_SEQUENCE *seq = AnmViewerGetCurrentSequence(data);
	if (seq == NULL) return;

	if (i < 0) i = 0;
	if (i > seq->nFrames) i = seq->nFrames;

	seq->nFrames++;
	seq->frames = (FRAME_DATA *) realloc(seq->frames, seq->nFrames * sizeof(FRAME_DATA));
	memmove(&seq->frames[i + 1], &seq->frames[i], (seq->nFrames - i - 1) * sizeof(FRAME_DATA));

	const unsigned int frameSizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_T) };

	FRAME_DATA *dest = &seq->frames[i];
	dest->nFrames = 4;
	dest->pad_ = 0xBEEF;
	dest->animationData = malloc(frameSizes[seq->type & 0xFFFF]);

	//init
	int oldI = data->currentFrame;
	ANIM_DATA_SRT frm = { 0 };
	frm.sx = FX32_ONE;
	frm.sy = FX32_ONE;
	data->currentFrame = i;
	AnmViewerPutCurrentAnimFrame(data, &frm, NULL);
	data->currentFrame = oldI;
	AnmViewerSetCurrentFrame(data, i, TRUE);
	AnmViewerSetDefaultAnchor(data);
}

static void AnmViewerCmdInsertFrameAbove(NANRVIEWERDATA *data) {
	AnmViewerInsertFrame(data, data->currentFrame);
}

static void AnmViewerCmdInsertFrameBelow(NANRVIEWERDATA *data) {
	AnmViewerInsertFrame(data, data->currentFrame + 1);
}

static void AnmViewerCmdInterpolateBelow(NANRVIEWERDATA *data) {
	NANR_SEQUENCE *seq = AnmViewerGetCurrentSequence(data);
	if (seq == NULL) return;

	int frame0 = data->currentFrame;
	int frame1 = frame0 + 1;

	//bound check
	if (frame0 < 0 || frame0 >= seq->nFrames) return;
	if (frame1 < 0 || frame1 >= seq->nFrames) return;

	//prompt interpolation parameters
	AnmViewerInterpolateSetting setting = { 0 };
	setting.linear = 0;        // default: not linear
	setting.clockwise = 1;     // default: rotations clockwise
	setting.nFrames = 1;       // default: 1 generated frame
	setting.totalDuration = 4; // default: 4 frames duration
	AnmViewerGetAnimFrame(data, data->currentAnim, frame0, &setting.start, NULL);
	AnmViewerGetAnimFrame(data, data->currentAnim, frame1, &setting.end, NULL);

	int status = AnmViewerPromptInterpolation(data, &setting);
	if (!status) return;

	//put
	seq->nFrames += setting.nResult;
	seq->frames = (FRAME_DATA *) realloc(seq->frames, seq->nFrames * sizeof(FRAME_DATA));
	memmove(&seq->frames[frame1 + setting.nResult], &seq->frames[frame1], (seq->nFrames - frame1 - setting.nResult) * sizeof(FRAME_DATA));

	const unsigned int frameSizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_T) };
	FRAME_DATA *dest = &seq->frames[frame1];
	for (int i = 0; i < setting.nResult; i++) {
		dest[i].pad_ = 0xBEEF;
		dest[i].nFrames = setting.durations[i];
		dest[i].animationData = calloc(1, frameSizes[seq->type & 0xFFFF]);
		data->currentFrame = frame1 + i;
		AnmViewerPutCurrentAnimFrame(data, &setting.result[i], NULL);
	}
	AnmViewerSetCurrentFrame(data, frame0, TRUE);
	AnmViewerSetDefaultAnchor(data);
	InvalidateRect(data->hWndAnimList, NULL, FALSE);

	free(setting.result);
	free(setting.durations);
}

static LRESULT CALLBACK AnmViewerFrameListProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);

	switch (msg) {
		case NV_INITIALIZE:
		{
			data = (NANRVIEWERDATA *) lParam;
			SetWindowLongPtr(hWnd, 0, lParam);
			data->hWndFrameList = CreateVirtualListView(hWnd, 0, 0, 300, 30);
			SetWindowSubclass(data->hWndFrameList, AnmViewerFrameListSubclassProc, 1, (DWORD_PTR) data);

			AddListViewColumn(data->hWndFrameList, L"#", 0, 25, SCA_LEFT);
			AddListViewColumn(data->hWndFrameList, L"Time", 1, 50, SCA_LEFT);
			AddListViewColumn(data->hWndFrameList, L"Cell", 2, 50, SCA_LEFT);
			AddListViewColumn(data->hWndFrameList, L"X", 3, 40, SCA_LEFT);         // no decimals
			AddListViewColumn(data->hWndFrameList, L"Y", 4, 40, SCA_LEFT);         // no decimals
			AddListViewColumn(data->hWndFrameList, L"Scale X", 5, 60, SCA_LEFT);   // 4 decimals
			AddListViewColumn(data->hWndFrameList, L"Scale Y", 6, 60, SCA_LEFT);   // 4 decimals
			AddListViewColumn(data->hWndFrameList, L"Rotation", 7, 75, SCA_LEFT);  // 3 decimals (degrees)
			SetFocus(data->hWndFrameList);

			DWORD dwStyle = (GetWindowLong(data->hWndFrameList, GWL_STYLE) | LVS_SHOWSELALWAYS) & ~(LVS_EDITLABELS);
			SetWindowLong(data->hWndFrameList, GWL_STYLE, dwStyle);

			//set virtual
			NANR_SEQUENCE *seq = AnmViewerGetCurrentSequence(data);
			if (seq != NULL) {
				ListView_SetItemCount(data->hWndFrameList, seq->nFrames);
				AnmViewerFrameListSelectFrame(data, data->currentFrame);
			}

			//fall through
		}
		case WM_SIZE:
		{
			if (data == NULL) break;

			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			MoveWindow(data->hWndFrameList, 0, 0, rcClient.right, rcClient.bottom, TRUE);
			break;
		}
		case WM_MDIACTIVATE:
		{
			if (data == NULL) break;
			if ((HWND) lParam == hWnd && LOWORD(wParam) != WA_INACTIVE) {
				SetFocus(data->hWndFrameList);
			}
			break;
		}
		case WM_NOTIFY:
		{
			if (data == NULL) break;

			NMHDR *hdr = (NMHDR *) lParam;
			if (hdr->hwndFrom == data->hWndFrameList) {
				switch (hdr->code) {
					case NM_CLICK:
					case NM_DBLCLK:
					case NM_RCLICK:
					case NM_RDBLCLK:
					{
						LPNMITEMACTIVATE nma = (LPNMITEMACTIVATE) hdr;
						if (nma->iItem == -1) {
							//item being unselected. Mark variable to cancel the deselection.
							ListView_SetItemState(data->hWndFrameList, data->currentFrame, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
						}
						break;
					}
					case LVN_GETDISPINFO:
					{
						NMLVDISPINFO *di = (NMLVDISPINFO *) lParam;
						
						if (di->item.mask & LVIF_COLUMNS) {
							di->item.cColumns = 6;
							di->item.puColumns[0] = 1;
							di->item.puColumns[1] = 2;
							di->item.puColumns[3] = 3;
							di->item.puColumns[4] = 4;
							di->item.puColumns[5] = 5;
						}

						if (di->item.mask & LVIF_TEXT) {
							ANIM_DATA_SRT srt;
							int duration;
							int s = AnmViewerGetAnimFrame(data, data->currentAnim, di->item.iItem, &srt, &duration);

							if (s && di->item.iSubItem < 8) {
								di->item.pszText = data->frameListBuffers[di->item.iSubItem];
								switch (di->item.iSubItem) {
									case 0: wsprintfW(di->item.pszText, L"%d", di->item.iItem); break; // #
									case 1: wsprintfW(di->item.pszText, L"%d", duration); break;       // duration
									case 2: wsprintfW(di->item.pszText, L"%d", srt.index); break;      // Cell
									case 3: wsprintfW(di->item.pszText, L"%d", srt.px); break;         // X
									case 4: wsprintfW(di->item.pszText, L"%d", srt.py); break;         // Y
									case 5: FormatFxToString(di->item.pszText, srt.sx); break;         // Scale X
									case 6: FormatFxToString(di->item.pszText, srt.sy); break;         // Scale Y
									case 7: FormatAngleToString(di->item.pszText, srt.rotZ); break;    // Rotation
								}
							}
						}

						return TRUE;
					}
					case LVN_ITEMCHANGED:
					{
						LPNMLISTVIEW nm = (LPNMLISTVIEW) hdr;
						if (nm->uNewState & LVIS_SELECTED) {
							//selection changed
							AnmViewerSetCurrentFrame(data, nm->iItem, FALSE);
						}
						break;
					}
				}
			}

			break;
		}
		case WM_COMMAND:
		{
			if (lParam == 0 && HIWORD(wParam) == 0) {
				switch (LOWORD(wParam)) {
					case ID_ANMMENU_SETINDEX:
						AnmViewerCmdSetIndex(data);
						break;
					case ID_ANMMENU_SETDURATION:
						AnmViewerCmdSetDuration(data);
						break;
					case ID_ANMMENU_SETX:
						AnmViewerCmdSetX(data);
						break;
					case ID_ANMMENU_SETY:
						AnmViewerCmdSetY(data);
						break;
					case ID_ANMMENU_SETSCALEX:
						AnmViewerCmdSetScaleX(data);
						break;
					case ID_ANMMENU_SETSCALEY:
						AnmViewerCmdSetScaleY(data);
						break;
					case ID_ANMMENU_SETROTATION:
						AnmViewerCmdSetRotation(data);
						break;
					case ID_ANMMENU_DELETE:
						AnmViewerDeleteCurrentFrame(data);
						break;
					case ID_ANMMENU_INSERTABOVE:
						AnmViewerCmdInsertFrameAbove(data);
						break;
					case ID_ANMMENU_INSERTBELOW:
						AnmViewerCmdInsertFrameBelow(data);
						break;
					case ID_ANMMENU_INTERPOLATEBELOW:
						AnmViewerCmdInterpolateBelow(data);
						break;
					case ID_FILE_SAVE:
					case ID_FILE_SAVEAS:
						//bubble up
						PostMessage(data->hWnd, msg, wParam, lParam);
						break;
				}
				AnmViewerFrameListUpdate(data);
				InvalidateRect(data->hWndPreview, NULL, FALSE);
			} else if (lParam == 0 && HIWORD(wParam) == 1) {
				switch (LOWORD(wParam)) {
					case ID_ACCELERATOR_SAVE:
						//bubble up accelerator command
						PostMessage(data->hWnd, msg, wParam, lParam);
						break;
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			if (data != NULL) data->hWndFrames = NULL;
			if (data != NULL) data->hWndFrameList = NULL;
			break;
		}
	}

	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}


static LRESULT CALLBACK AnmViweerInterpProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) GetWindowLongPtr(hWnd, 0);

	switch (msg) {
		case NV_INITIALIZE:
		{
			data = (NANRVIEWERDATA *) lParam;
			SetWindowLongPtr(hWnd, 0, lParam);

			AnmViewerInterpolateSetting *setting = data->interpData;

			WCHAR framesbuf[16], durationbuf[16];
			wsprintfW(framesbuf, L"%d", setting->nFrames);
			wsprintfW(durationbuf, L"%d", setting->totalDuration);

			CreateStatic(hWnd, L"Generate Frames:", 10, 10, 100, 22);
			data->hWndInterpFrames = CreateEdit(hWnd, framesbuf, 110, 10, 75, 22, TRUE);
			CreateStatic(hWnd, L"Total Duration:", 10, 37, 100, 22);
			data->hWndInterpDuration = CreateEdit(hWnd, durationbuf, 110, 37, 75, 22, TRUE);
			data->hWndCheckboxLinear = CreateCheckbox(hWnd, L"Linear", 10, 64, 100, 22, setting->linear);
			data->hWndCheckboxClockwise = CreateCheckbox(hWnd, L"Clockwise", 10, 91, 100, 22, setting->clockwise);
			data->hWndInterpOK = CreateButton(hWnd, L"OK", 110, 116, 75, 22, TRUE);

			//in linear mode, disable clockwise setting
			if (setting->linear) EnableWindow(data->hWndCheckboxClockwise, FALSE);

			SetGUIFont(hWnd);
			SetWindowSize(hWnd, 100 + 75 + 20, 116 + 22 + 10);
			SetFocus(data->hWndInterpFrames);
			break;
		}
		case WM_COMMAND:
		{
			if (data == NULL) break;
			AnmViewerInterpolateSetting *setting = data->interpData;

			HWND hWndCtl = (HWND) lParam;
			int notif = HIWORD(wParam);
			if (hWndCtl == data->hWndCheckboxLinear && notif == BN_CLICKED) {
				int state = GetCheckboxChecked(hWndCtl);
				setting->linear = state;

				EnableWindow(data->hWndCheckboxClockwise, !setting->linear);
				InvalidateRect(data->hWndCheckboxClockwise, NULL, FALSE);
			} else if (hWndCtl == data->hWndCheckboxClockwise && notif == BN_CLICKED) {
				int state = GetCheckboxChecked(hWndCtl);
				setting->clockwise = state;
			} else if ((hWndCtl == data->hWndInterpOK || LOWORD(wParam) == IDOK) && notif == BN_CLICKED) {

				//fill out interpolation
				unsigned int nFrames = GetEditNumber(data->hWndInterpFrames);
				unsigned int totalDuration = GetEditNumber(data->hWndInterpDuration);

				setting->nFrames = nFrames;
				setting->totalDuration = totalDuration;
				setting->nResult = nFrames;
				setting->durations = (int *) calloc(nFrames, sizeof(int));
				setting->result = (ANIM_DATA_SRT *) calloc(nFrames, sizeof(ANIM_DATA_SRT));

				//evenly divide total duration
				unsigned int nFramesDenom = nFrames;
				for (unsigned int i = 0; i < nFrames; i++) {
					setting->durations[i] = (2 * totalDuration + nFramesDenom) / (2 * nFramesDenom);

					nFramesDenom--;
					totalDuration -= setting->durations[i];
				}

				//get rotation parameter
				unsigned int rot0 = setting->start.rotZ;
				unsigned int rot1 = setting->end.rotZ;
				if (setting->clockwise) {
					//clockwise: ensure rot1 >= rot0
					if (rot1 < rot0) {
						rot1 += 65536;
					}
				} else {
					//counterclockwise: ensure rot0 >= rot1
					if (rot0 < rot1) {
						rot0 += 65536;
					}
				}

				//initial matrix transformation
				double mtxA0 = (setting->start.sx / 4096.0f) * cos(RAD_360DEG * setting->start.rotZ / 65536.0f);
				double mtxA1 = (setting->end.sx / 4096.0f) * cos(RAD_360DEG * setting->end.rotZ / 65536.0f);
				double mtxB0 = (setting->start.sy / 4096.0f) * sin(RAD_360DEG * setting->start.rotZ / 65536.0f);
				double mtxB1 = (setting->end.sy / 4096.0f) * sin(RAD_360DEG * setting->end.rotZ / 65536.0f);
				double mtxC0 = (setting->start.sx / 4096.0f) * -sin(RAD_360DEG * setting->start.rotZ / 65536.0f);
				double mtxC1 = (setting->end.sx / 4096.0f) * -sin(RAD_360DEG * setting->end.rotZ / 65536.0f);
				double mtxD0 = (setting->start.sy / 4096.0f) * cos(RAD_360DEG * setting->start.rotZ / 65536.0f);
				double mtxD1 = (setting->end.sy / 4096.0f) * cos(RAD_360DEG * setting->end.rotZ / 65536.0f);

				//write interpolation
				for (unsigned int i = 0; i < nFrames; i++) {
					int weight0 = 2 * (nFrames - i);
					int weight1 = 2 * (i + 1);
					int totalWeight = weight0 + weight1;

					//set index, px, py (same whether linear or not)
					int px = setting->start.px * weight0 + setting->end.px * weight1;
					int py = setting->start.py * weight0 + setting->end.py * weight1;

					setting->result[i].index = (setting->start.index * weight0 + setting->end.index * weight1 + totalWeight / 2) / totalWeight;
					setting->result[i].px = (px + (px < 0 ? -totalWeight : totalWeight) / 2) / totalWeight;
					setting->result[i].py = (py + (py < 0 ? -totalWeight : totalWeight) / 2) / totalWeight;

					if (setting->linear) {
						//linear: interpolate linearly
						double mtxA = (mtxA0 * weight0 + mtxA1 * weight1) / ((double) totalWeight);
						double mtxB = (mtxB0 * weight0 + mtxB1 * weight1) / ((double) totalWeight);
						double mtxC = (mtxC0 * weight0 + mtxC1 * weight1) / ((double) totalWeight);
						double mtxD = (mtxD0 * weight0 + mtxD1 * weight1) / ((double) totalWeight);

						//correct for scale, ensure valid bounds
						double sxMag = sqrt(mtxA * mtxA + mtxC * mtxC);
						double syMag = sqrt(mtxB * mtxB + mtxD * mtxD);
						if (sxMag > 0.0f) {
							mtxA /= sxMag;
							mtxC /= sxMag;
						}
						if (syMag > 0.0f) {
							mtxB /= syMag;
							mtxD /= syMag;
						}

						if (mtxA > 1.0f) mtxA = 1.0f; if (mtxA < -1.0f) mtxA = -1.0f;
						if (mtxB > 1.0f) mtxB = 1.0f; if (mtxB < -1.0f) mtxB = -1.0f;
						if (mtxC > 1.0f) mtxC = 1.0f; if (mtxC < -1.0f) mtxC = -1.0f;
						if (mtxD > 1.0f) mtxD = 1.0f; if (mtxD < -1.0f) mtxD = -1.0f;

						//if matrix B and C have the same sign, then flip sign of ScaleY.
						double angleMult = 1.0;
						if ((mtxA < 0.0f && mtxD > 0.0f) || (mtxA > 0.0f && mtxD < 0.0f)) {
							syMag = -syMag;
							mtxC = -mtxC;
							mtxD = -mtxD;
							angleMult = -1.0;
						}

						//mtxA and mtxB both scaled by Sx, so have the same sign w.r.t. each other.
						double angle = acos(mtxA);
						if (mtxC >= 0.0f) {
							//other half of circle
							angle = -angle;
						}
						angle *= angleMult;

						setting->result[i].sx = FloatToInt(sxMag * 4096.0);
						setting->result[i].sy = FloatToInt(syMag * 4096.0);
						setting->result[i].rotZ = FloatToInt(angle * 65536.0 / RAD_360DEG) & 0xFFFF;
					} else {
						//nonlinear: interpolate each parameter
						int sx = setting->start.sx * weight0 + setting->end.sx * weight1;
						int sy = setting->start.sy * weight0 + setting->end.sy * weight1;

						setting->result[i].sx = (sx + (sx < 0 ? -totalWeight : totalWeight) / 2) / totalWeight;
						setting->result[i].sy = (sy + (sy < 0 ? -totalWeight : totalWeight) / 2) / totalWeight;
						setting->result[i].rotZ = ((rot0 * weight0 + rot1 * weight1 + totalWeight / 2) / totalWeight) & 0xFFFF;
					}
				}

				data->interpResult = 1;
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			} else if (LOWORD(wParam) == IDCANCEL && notif == BN_CLICKED) {
				SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static int AnmViewerPromptInterpolation(NANRVIEWERDATA *data, AnmViewerInterpolateSetting *setting) {
	HWND hWndMain = data->editorMgr->hWnd;
	HWND h = CreateWindow(L"NanrInterpClass", L"Create Interpolation", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX),
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		hWndMain, NULL, NULL, NULL);
	data->interpData = setting;
	data->interpResult = 0;
	SendMessage(h, NV_INITIALIZE, 0, (LPARAM) data);
	ShowWindow(h, SW_SHOW);
	DoModal(h);

	return data->interpResult;
}



void AnmViewerUpdateCellBounds(HWND hWnd) {
	NANRVIEWERDATA *data = (NANRVIEWERDATA *) EditorGetData(hWnd);
	AnmViewerSetDefaultAnchor(data);
	InvalidateRect(data->hWndPreview, NULL, FALSE);
}

void RegisterNanrViewerClass(void) {
	AnmRegisterFormats();

	int features = EDITOR_FEATURE_ZOOM | EDITOR_FEATURE_GRIDLINES;
	EDITOR_CLASS *cls = EditorRegister(L"NanrViewerClass", AnmViewerWndProc, L"Animation Editor", sizeof(NANRVIEWERDATA), features);
	EditorAddFilter(cls, NANR_TYPE_NANR, L"nanr", L"NANR Files (*.nanr)\0*.nanr\0");
	EditorAddFilter(cls, NANR_TYPE_GHOSTTRICK, L"bin", L"Ghost Trick Files (*.bin)\0*.bin\0");
	RegisterGenericClass(L"NanrPreviewClass", AnmViewerPreviewWndProc, sizeof(void *));
	RegisterGenericClass(L"NanrFrameClass", AnmViewerFrameListProc, sizeof(void *));
	RegisterGenericClass(L"NanrInterpClass", AnmViweerInterpProc, sizeof(void *));
}

static HWND CreateNanrViewerInternal(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path, NANR *nanr) {
	HWND h = EditorCreate(L"NanrViewerClass", x, y, width, height, hWndParent);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) nanr);
	return h;
}

HWND CreateNanrViewer(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	NANR *nanr = (NANR *) calloc(1, sizeof(NANR));
	if (AnmReadFile(nanr, path)) {
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}
	return CreateNanrViewerInternal(x, y, width, height, hWndParent, path, nanr);
}

HWND CreateNanrViewerImmediate(int x, int y, int width, int height, HWND hWndParent, NANR *nanr) {
	return CreateNanrViewerInternal(x, y, width, height, hWndParent, NULL, nanr);
}
