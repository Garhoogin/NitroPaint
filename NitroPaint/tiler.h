#pragma once
#include <Windows.h>

HBITMAP CreateTileBitmap(LPVOID lpBits, UINT nWidth, UINT nHeight, int hiliteX, int hiliteY, PUINT pOutWidth, PUINT pOutHeight, UINT scale, BOOL bBorders);