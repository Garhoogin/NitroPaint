#pragma once
#include "ncer.h"

typedef struct OBJ_BOUNDS_ {
	int x;
	int y;
	int width;
	int height;
} OBJ_BOUNDS;

typedef struct OBJ_IMAGE_SLICE_ {
	OBJ_BOUNDS bounds;
	COLOR32 px[64 * 64];
} OBJ_IMAGE_SLICE;

OBJ_BOUNDS *CellgenMakeCell(COLOR32 *px, int width, int height, int aggressiveness, int full, int *pnObj);
OBJ_IMAGE_SLICE *CellgenSliceImage(COLOR32 *px, int width, int height, OBJ_BOUNDS *bounds, int nObj);
void CellgenGetBounds(COLOR32 *px, int width, int height, int *pxMin, int *pxMax, int *pyMin, int *pyMax);
