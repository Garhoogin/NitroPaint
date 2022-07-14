#include "color.h"

static uint8_t ColorConvertsionLookupReverse[] = {
	0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,
	2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,
	4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,
	6,  6,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,
	8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,  9,  10,
	10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 12,
	12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15,
	16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17,
	18, 18, 18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19,
	19, 20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21,
	21, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23,
	23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25,
	25, 25, 26, 26, 26, 26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27,
	27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29, 29, 29,
	29, 29, 29, 30, 30, 30, 30, 30, 30, 30, 30, 31, 31, 31, 31, 31
};

COLOR ColorConvertToDS(COLOR32 c) {
	int r = ColorConvertsionLookupReverse[c & 0xFF];
	int g = ColorConvertsionLookupReverse[(c >> 8) & 0xFF];
	int b = ColorConvertsionLookupReverse[(c >> 16) & 0xFF];
	return r | (g << 5) | (b << 10);
}

static uint8_t colorConversionLookup[] = {
	0,   8,   16,  25,  33,  41,  49,  58,
	66,  74,  82,  90,  99,  107, 115, 123,
	132, 140, 148, 156, 165, 173, 181, 189,
	197, 206, 214, 222, 230, 239, 247, 255
};

COLOR32 ColorConvertFromDS(COLOR c) {
	int r = colorConversionLookup[GetR(c)];
	int g = colorConversionLookup[GetG(c)];
	int b = colorConversionLookup[GetB(c)];
	return r | (g << 8) | (b << 16);
}

COLOR ColorInterpolate(COLOR c1, COLOR c2, float amt) {
	int r = (int) (GetR(c1) * (1.0f - amt) + GetR(c2) * amt);
	int g = (int) (GetG(c1) * (1.0f - amt) + GetG(c2) * amt);
	int b = (int) (GetB(c1) * (1.0f - amt) + GetB(c2) * amt);
	return r | (g << 5) | (b << 10);
}