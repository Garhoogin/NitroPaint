#pragma once
#include <Windows.h>

HBITMAP CreateTileBitmap(LPDWORD lpBits, UINT nWidth, UINT nHeight, int hiliteX, int hiliteY, PUINT pOutWidth, PUINT pOutHeight, UINT scale, BOOL bBorders);

HBITMAP CreateTileBitmap2(LPDWORD lpBits, UINT nWidth, UINT nHeight, int hiliteX, int hiliteY, PUINT pOutWidth, PUINT pOutHeight, UINT scale, BOOL bBorders, UINT tileWidth, BOOL bReverseColors, BOOL bAlphaBlend);

int getDimension2(int tiles, int border, int scale, int tileSize);

int getDimension(int tiles, int border, int scale);