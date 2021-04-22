#pragma once

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
#define TEXS(p)			(8<<(((p)>>20)&7))
#define TEXT(p)			(8<<(((p)>>23)&7))

typedef struct {
	int texImageParam;
	char *texel;
	short *cmp;
	char name[16]; //NOT necessarily null terminated!
} TEXELS;

typedef struct {
	int nColors;
	short *pal;
	char name[16]; //NOT necessarily null terminated!
} PALETTE;

typedef struct {
	TEXELS texels;
	PALETTE palette;
} TEXTURE;

char *stringFromFormat(int fmt);

void convertTexture(DWORD *px, TEXELS *texels, PALETTE *palette, int flip);

int getTexelSize(int width, int height, int texImageParam);

void writeNitroTGA(LPWSTR name, TEXELS *texels, PALETTE *palette);

int textureDimensionIsValid(int x);

int nitrotgaIsValid(unsigned char *buffer, int size);

int nitroTgaRead(LPWSTR path, TEXELS *texels, PALETTE *palette);