#include "color.h"

COLOR ColorConvertToDS(COLOR32 c) {
	int r = ((c & 0xFF) * 62 + 255) / 510;
	int g = (((c >> 8) & 0xFF) * 62 + 255) / 510;
	int b = (((c >> 16) & 0xFF) * 62 + 255) / 510;
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