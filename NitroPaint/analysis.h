#pragma once
#include <Windows.h>

DWORD getAverageColor(DWORD *colors, int nColors);

void getPrincipalComponent(DWORD *colors, int nColors, float *vec);

void getColorEndPoints(DWORD *colors, int nColors, DWORD *points);