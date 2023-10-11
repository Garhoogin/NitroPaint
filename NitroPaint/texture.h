#pragma once
#include "color.h"

#define CT_A3I5 1			/*can read and write*/
#define CT_4COLOR 2			/*can read and write*/
#define CT_16COLOR 3		/*can read and write*/
#define CT_256COLOR 4		/*can read and write*/
#define CT_4x4 5			/*can read and write*/
#define CT_A5I3 6			/*can read and write*/
#define CT_DIRECT 7			/*can read and write*/


#define FORMAT(p)		(((p)>>26)&7)
#define COL0TRANS(p)	(((p)>>29)&1)
#define OFFSET(p)		(((p)&0xFFFF)<<3)
#define TEXW(p)			(8<<(((p)>>20)&7))
#define TEXH(p)			(8<<(((p)>>23)&7))

//4x4 compression macros
#define COMP_INTERPOLATE   0x4000
#define COMP_FULL          0x0000
#define COMP_OPAQUE        0x8000
#define COMP_TRANSPARENT   0x0000
#define COMP_MODE_MASK     0xC000
#define COMP_INDEX_MASK    0x3FFF
#define COMP_INDEX(c)      (((c)&COMP_INDEX_MASK)<<1)

typedef struct {
	int texImageParam;
	int height;
	char *texel;
	short *cmp;
	char name[16]; //NOT necessarily null terminated!
} TEXELS;

typedef struct {
	int nColors;
	COLOR *pal;
	char name[16]; //NOT necessarily null terminated!
} PALETTE;

typedef struct {
	TEXELS texels;
	PALETTE palette;
} TEXTURE;

const char *TxNameFromTexFormat(int fmt);

void TxRender(COLOR32 *px, int dstWidth, int dstHeight, TEXELS *texels, PALETTE *palette, int flip);

int TxGetTexelSize(int width, int height, int texImageParam);

int TxGetTextureVramSize(TEXELS *texels);

int TxGetIndexVramSize(TEXELS *texels);

int TxGetTexPlttVramSize(PALETTE *palette);

void TxWriteNnsTga(LPCWSTR name, TEXELS *texels, PALETTE *palette);

int TxDimensionIsValid(int x);

int TxIsValidNnsTga(const unsigned char *buffer, unsigned int size);

int TxReadNnsTga(LPCWSTR path, TEXELS *texels, PALETTE *palette);