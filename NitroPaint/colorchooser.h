#pragma once
#include <Windows.h>

BOOL WINAPI CustomChooseColor(CHOOSECOLORW *chooseColor);

VOID ConvertRGBToHSV(COLORREF col, int *h, int *s, int *v);

VOID ConvertHSVToRGB(int h, int s, int v, COLORREF *rgb);