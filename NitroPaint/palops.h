#pragma once
#include <Windows.h>

typedef struct PAL_OP_ {
	HWND hWndParent;
	int hueRotate;
	int valueAdd;
	int saturationAdd;
	int paletteRotation;
	int srcIndex;
	int srcLength;
	int ignoreFirst;
	int dstOffset;
	int dstCount;
	int dstStride;
	int result;
	void *param;
	void (*updateCallback) (struct PAL_OP_ *palOp);
} PAL_OP;

void PalopRunOperation(COLOR *palIn, COLOR *palOut, int palSize, PAL_OP *op);

int SelectPaletteOperation(PAL_OP *opStruct);
