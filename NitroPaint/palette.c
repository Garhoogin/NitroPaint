#include "palette.h"
#include <math.h>

int RxColorLightnessComparator(const void *d1, const void *d2) {
	COLOR32 c1 = *(COLOR32 *) d1;
	COLOR32 c2 = *(COLOR32 *) d2;
	if (c1 == c2) return 0;
	
	//by properties of linear transformations, this is valid
	int dr = (c1 & 0xFF) - (c2 & 0xFF);
	int dg = ((c1 >> 8) & 0xFF) - ((c2 >> 8) & 0xFF);
	int db = ((c1 >> 16) & 0xFF) - ((c2 >> 16) & 0xFF);
	int dy = dr * 299 + dg * 587 + db * 114;

	return dy;
}

void RxCreatePaletteTransparentReserve(COLOR32 *img, int width, int height, COLOR32 *pal, int nColors) {
	RxCreatePalette(img, width, height, pal + 1, nColors - 1);
	*pal = 0xFF00FF;
}

int RxPaletteFindClosestColorSimple(COLOR32 rgb, COLOR32 *palette, int paletteSize) {
	int smallestDistance = 1 << 24;
	int index = 0, i = 0;
	int ey, eu, ev;

	//test exact matches
	for (i = 0; i < paletteSize; i++) {
		if ((rgb & 0xFFFFFF) == (palette[i] & 0xFFFFFF)) {
			return i;
		}
	}

	//else
	for (i = 0; i < paletteSize; i++) {
		COLOR32 entry = palette[i];
		int dr = ((entry >>  0) & 0xFF) - ((rgb >>  0) & 0xFF);
		int dg = ((entry >>  8) & 0xFF) - ((rgb >>  8) & 0xFF);
		int db = ((entry >> 16) & 0xFF) - ((rgb >> 16) & 0xFF);

		RxConvertRgbToYuv(dr, dg, db, &ey, &eu, &ev);
		int dst = 4 * ey * ey + eu * eu + ev * ev;

		if (dst < smallestDistance) {
			index = i;
			smallestDistance = dst;
		}
	}

	return index;
}

static int m(int a) {
	return a < 0? 0: (a > 255? 255: a);
}

#define diffuse(a,r,g,b,ap) a=m((int)((a&0xFF)+(r)))|(m((int)(((a>>8)&0xFF)+(g)))<<8)|(m((int)(((a>>16)&0xFF)+(b)))<<16)|(m((int)(((a>>24)&0xFF)+(ap)))<<24)

void doDiffuse(int i, int width, int height, unsigned int * pixels, int errorRed, int errorGreen, int errorBlue, int errorAlpha, float amt) {
	//if ((pixels[i] >> 24) < 127) return;
	if (i % width < width - 1) {
		unsigned int right = pixels[i + 1];
		diffuse(right, errorRed * 7 * amt / 16, errorGreen * 7 * amt / 16, errorBlue * 7 * amt / 16, errorAlpha * 7 * amt / 16);
		pixels[i + 1] = right;
	}
	if (i / width < height - 1) {
		if (i % width > 0) {//downleft
			unsigned int right = pixels[i + width - 1];
			diffuse(right, errorRed * 3 * amt / 16, errorGreen * 3 * amt / 16, errorBlue * 3 * amt / 16, errorAlpha * 3 * amt / 16);
			pixels[i + width - 1] = right;
		}
		if (1) {//down
			unsigned int right = pixels[i + width];
			diffuse(right, errorRed * 5 * amt / 16, errorGreen * 5 * amt / 16, errorBlue * 5 * amt / 16, errorAlpha * 5 * amt / 16);
			pixels[i + width] = right;
		}
		if (i % width < width - 1) {
			unsigned int right = pixels[i + width + 1];
			diffuse(right, errorRed * 1 * amt / 16, errorGreen * 1 * amt / 16, errorBlue * 1 * amt / 16, errorAlpha * 1 * amt / 16);
			pixels[i + width + 1] = right;
		}
	}
}

void RxConvertRgbToYuv(int r, int g, int b, int *y, int *u, int *v) {
	*y = (int) ( 0.2990 * r + 0.5870 * g + 0.1140 * b);
	*u = (int) (-0.1684 * r - 0.3316 * g + 0.5000 * b);
	*v = (int) ( 0.5000 * r - 0.4187 * g - 0.0813 * b);
}

void RxConvertYuvToRgb(int y, int u, int v, int *r, int *g, int *b) {
	*r = (int) (y - 0.00004f * u + 1.40199f * v);
	*g = (int) (y - 0.34408f * u - 0.71389f * v);
	*b = (int) (y + 1.77180f * u - 0.00126f * v);
}
